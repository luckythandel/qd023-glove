#!/usr/bin/env python3
"""Find devices that can be used as a QD023 synthesizer output.

Reports four groups:

  1. THIS PC      audio output devices (speakers, headsets, virtual cables)
  2. ANDROID      phones/tablets attached to adb (USB or wireless debugging)
  3. BLUETOOTH    nearby BLE devices that look like phones or computers,
                  plus the glove itself and the control mode it advertises
  4. NETWORK      hosts on the local subnet exposing synth-ish ports
                  (RTP-MIDI 5004, OSC 9000, adb 5555, the bridge 8787, ...)

Usage:
    python scan_synth.py              # full scan
    python scan_synth.py --no-lan     # skip the subnet sweep (fast)
    python scan_synth.py --json       # machine readable

audio enumeration runs in a child process on purpose: importing
sounddevice initialises PortAudio, which pins the calling thread to a
single-threaded COM apartment and breaks bleak's WinRT callbacks.
"""
from __future__ import annotations

import argparse
import asyncio
import concurrent.futures
import json
import os
import shutil
import socket
import subprocess
import sys
import time

AUDIO_PROBE = r"""
import json, sys
try:
    import sounddevice as sd
except Exception as exc:
    print(json.dumps({"error": str(exc), "devices": []}))
    sys.exit(0)
out, seen = [], set()
try:
    default_out = sd.default.device[1]
except Exception:
    default_out = None
for idx, d in enumerate(sd.query_devices()):
    if int(d.get("max_output_channels", 0)) <= 0:
        continue
    api = str(sd.query_hostapis(d["hostapi"])["name"])
    if "WDM-KS" in api:
        continue
    name = str(d.get("name", "")).strip()
    if not name:
        continue
    key = (name.lower(), api)
    if key in seen:
        continue
    seen.add(key)
    out.append({
        "name": name,
        "hostapi": api,
        "channels": int(d.get("max_output_channels", 0)),
        "samplerate": int(d.get("default_samplerate", 0)),
        "default": idx == default_out,
    })
print(json.dumps({"devices": out}))
"""

PHONE_HINTS = (
    "phone", "galaxy", "pixel", "redmi", "oneplus", "oppo", "vivo", "xiaomi",
    "iphone", "ipad", "surface", "laptop", "pc", "book", "tab",
)

SYNTH_PORTS = {
    5004: "RTP-MIDI / AppleMIDI",
    9000: "OSC",
    5555: "adb over WiFi",
    8787: "QD023 bridge",
    80: "glove portal",
    62078: "iPhone sync",
    8080: "http-alt synth/app",
}


def local_ips() -> list[str]:
    """Every usable IPv4 address, so the sweep covers the real LAN too.

    A VPN or virtual adapter often owns the default route (10.5.0.2 here),
    which would hide the WiFi subnet the glove actually lives on.
    """
    ips: set[str] = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ip = info[4][0]
            if not ip.startswith("127."):
                ips.add(ip)
    except Exception:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.add(s.getsockname()[0])
        s.close()
    except Exception:
        pass
    private = [ip for ip in ips if ip.startswith(("10.", "192.168.", "172."))]
    return sorted(private) or sorted(ips)


def local_ip() -> str | None:
    ips = local_ips()
    return ips[0] if ips else None


def scan_audio() -> dict:
    try:
        p = subprocess.run(
            [sys.executable, "-c", AUDIO_PROBE],
            capture_output=True, text=True, timeout=25,
        )
        return json.loads(p.stdout.strip() or "{}")
    except Exception as exc:
        return {"error": str(exc), "devices": []}


def find_adb() -> str | None:
    found = shutil.which("adb")
    if found:
        return found
    for cand in (
        r"D:\android-sdk\platform-tools\adb.exe",
        r"C:\Android\platform-tools\adb.exe",
        os.path.expanduser(r"~\AppData\Local\Android\Sdk\platform-tools\adb.exe"),
    ):
        if os.path.isfile(cand):
            return cand
    return None


def scan_adb() -> list[dict]:
    adb = find_adb()
    if not adb:
        return []
    try:
        out = subprocess.run([adb, "devices", "-l"], capture_output=True,
                             text=True, timeout=15).stdout
    except Exception:
        return []
    rows = []
    for line in out.splitlines()[1:]:
        line = line.strip()
        if not line or line.startswith("*"):
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        serial, state = parts[0], parts[1]
        props = dict(
            p.split(":", 1) for p in parts[2:] if ":" in p
        )
        model = props.get("model", "").replace("_", " ")
        rows.append({
            "serial": serial,
            "state": state,
            "model": model,
            "transport": "wifi" if ":" in serial else "usb",
        })
    return rows


async def scan_ble(seconds: float) -> list[dict]:
    try:
        from bleak import BleakScanner
    except Exception as exc:
        return [{"error": f"bleak unavailable: {exc}"}]

    seen: dict[str, dict] = {}

    def cb(dev, adv):
        name = adv.local_name or dev.name or ""
        md = (getattr(adv, "manufacturer_data", None) or {}).get(0xFFFF)
        mode = None
        if md:
            mode = {0: "WINDOWS", 1: "LED", 2: "ANDROID", 3: "MUSIC"}.get(md[0])
        if not name and mode is None:
            return
        seen[dev.address] = {
            "address": dev.address,
            "name": name,
            "rssi": adv.rssi,
            "glove_mode": mode,
            "services": [str(u) for u in (adv.service_uuids or [])][:4],
        }

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    await asyncio.sleep(seconds)
    await scanner.stop()

    rows = list(seen.values())
    for r in rows:
        low = r["name"].lower()
        r["synthesizer_candidate"] = bool(
            r["glove_mode"]
            or any(h in low for h in PHONE_HINTS)
        )
    rows.sort(key=lambda r: (not r["synthesizer_candidate"], -r["rssi"]))
    return rows


def _probe(host: str, port: int, timeout: float) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except Exception:
        return False


def scan_lan(timeout: float = 0.3) -> list[dict]:
    ips = local_ips()
    if not ips:
        return []
    bases = sorted({ip.rsplit(".", 1)[0] for ip in ips})
    quick_ports = (80, 8787, 5555, 62078)
    hits: list[dict] = []

    def work(host: str):
        if not any(_probe(host, p, timeout) for p in quick_ports):
            return None
        return {"host": host, "ports": [p for p in SYNTH_PORTS if _probe(host, p, timeout)]}

    for base in bases:
        hosts = [f"{base}.{i}" for i in range(1, 255)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=96) as pool:
            for res in pool.map(work, hosts):
                if res and res["ports"]:
                    hits.append(res)
    hits.sort(key=lambda r: r["host"])
    return hits


def print_report(audio: dict, android: list, ble: list, lan: list, ip: str | None) -> None:
    print("=" * 68)
    print(" QD023 // SYNTHESIZER TARGETS")
    print("=" * 68)

    print("\n[1] THIS PC - audio outputs")
    devs = audio.get("devices", [])
    if audio.get("error"):
        print(f"    ! {audio['error']}")
    if not devs:
        print("    (none found)")
    for d in devs:
        mark = "  <- default" if d.get("default") else ""
        print(f"    - {d['name']}")
        print(f"      {d['hostapi']}  {d['channels']}ch  {d['samplerate']} Hz{mark}")

    print("\n[2] ANDROID - devices on adb (can run the phone synth)")
    if not android:
        print("    (none attached - enable USB debugging or wireless debugging)")
    for d in android:
        print(f"    - {d['model'] or 'unknown model'}  [{d['transport']}]  {d['serial']}  {d['state']}")

    print("\n[3] BLUETOOTH - nearby devices")
    if not ble:
        print("    (none)")
    for d in ble:
        tag = " <-- glove" if d.get("glove_mode") else ""
        cand = "*" if d.get("synthesizer_candidate") else " "
        print(f"   {cand} {d['name'] or '(no name)':<28} {d['rssi']:>4} dBm  {d['address']}")
        if d.get("glove_mode"):
            print(f"       mode: {d['glove_mode']}{tag}")

    print("\n[4] NETWORK - synth-capable hosts on the local subnet")
    print(f"    scanning from {', '.join(local_ips()) or 'unknown'}")
    if not lan:
        print("    (nothing answered)")
    for h in lan:
        ports = ", ".join(f"{p} ({SYNTH_PORTS[p]})" for p in h["ports"])
        print(f"    - {h['host']}: {ports}")

    print("\nLegend: * = plausible synthesizer output")
    print("Run the bridge with --music to synthesize on this PC,")
    print("or connect the Android app in MUSIC mode to play on the phone.")
    print("=" * 68)


def main() -> int:
    ap = argparse.ArgumentParser(description="Scan for QD023 synthesizer targets")
    ap.add_argument("--json", action="store_true", help="machine readable output")
    ap.add_argument("--no-lan", action="store_true", help="skip the subnet sweep")
    ap.add_argument("--ble-seconds", type=float, default=8.0)
    args = ap.parse_args()

    t0 = time.time()
    ip = local_ip()
    audio = scan_audio()
    android = scan_adb()
    ble = asyncio.run(scan_ble(args.ble_seconds))
    lan = [] if args.no_lan else scan_lan()

    if args.json:
        print(json.dumps({
            "host_ip": ip,
            "audio": audio,
            "android": android,
            "bluetooth": ble,
            "network": lan,
            "elapsed_s": round(time.time() - t0, 1),
        }, indent=2))
    else:
        print_report(audio, android, ble, lan, ip)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
