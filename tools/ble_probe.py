"""
BLE probe for QD023-Glove.

Scans, connects, subscribes to the TX characteristic and prints every message
for a few seconds. Use this to prove the PC<->glove link works before running
the full receiver.

Usage:
    python ble_probe.py               # scan + connect + listen 20s
    python ble_probe.py --seconds 60  # listen longer
    python ble_probe.py --address AA:BB:CC:DD:EE:FF
"""
import argparse
import asyncio
import sys

NAME = "QD023-Glove"
SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
KNOWN_ADDRESS = "68:09:47:36:21:92"


def adv_name(adv, dev):
    return (getattr(adv, "local_name", None) or getattr(dev, "name", None) or "")


async def discover_all(timeout=12):
    """Return list of (device, adv) for every visible BLE device."""
    found = []
    try:
        result = await asyncio.wait_for(
            __import__("bleak").BleakScanner.discover(timeout=timeout, return_adv=True),
            timeout=timeout + 5,
        )
    except Exception as exc:  # noqa: BLE001
        print(f"scan error: {exc}")
        return found
    if isinstance(result, dict):
        for _addr, pair in result.items():
            try:
                dev, adv = pair
            except Exception:
                continue
            found.append((dev, adv))
    else:
        for dev in result or []:
            found.append((dev, None))
    return found


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=20)
    ap.add_argument("--address", default="")
    ap.add_argument("--scan-only", action="store_true")
    ap.add_argument("--write-cal", action="store_true", help="send 'CAL' after connect")
    args = ap.parse_args()

    from bleak import BleakClient, BleakScanner

    target = None

    if args.address:
        print(f"using explicit address {args.address}")
        target = args.address
    else:
        print(f"scanning {NAME} ...")
        devices = await discover_all()
        print(f"  {len(devices)} BLE device(s) visible")
        for dev, adv in devices:
            nm = adv_name(adv, dev)
            if not nm and adv is not None:
                nm = ""
            rssi = getattr(adv, "rssi", None) if adv else None
            if nm:
                print(f"    {dev.address:20s} rssi={rssi}  {nm}")
            if nm and nm.strip().lower() == NAME.lower():
                target = dev
        if target is None:
            print(f"\n!! {NAME} not advertising.")
            print("   - is the glove powered and running the firmware (LED blinking)?")
            print("   - if Windows is already connected to it, it stops advertising.")
            print(f"   - trying the known address anyway: {KNOWN_ADDRESS}")
            target = KNOWN_ADDRESS

    if args.scan_only:
        return

    if isinstance(target, str):
        print(f"\nconnecting to {target} ...")
        client = BleakClient(target, timeout=25)
    else:
        print(f"\nconnecting to {target.address} ...")
        client = BleakClient(target, timeout=25)

    try:
        await client.connect()
    except Exception as exc:  # noqa: BLE001
        print(f"CONNECT FAILED: {exc}")
        print("  - Bluetooth ON?  - glove in range?  - another app connected to it?")
        sys.exit(1)

    print(f"CONNECTED  mtu={getattr(client, 'mtu_size', '?')}")

    try:
        services = client.services
        for s in services:
            print(f"  service {s.uuid}  ({s.description or ''})")
            for c in s.characteristics:
                print(f"     char {c.uuid}  props={','.join(c.properties)}")
    except Exception as exc:  # noqa: BLE001
        print(f"  (service enumeration failed: {exc})")

    got = {"n": 0}

    def on_rx(_sender, data: bytearray):
        txt = bytes(data).decode(errors="ignore").strip()
        for line in txt.splitlines():
            line = line.strip()
            if line:
                got["n"] += 1
                print(f"  <- {line}")

    try:
        await client.start_notify(TX_UUID, on_rx)
        print(f"\nsubscribed to TX; listening {args.seconds}s (move the glove) ...")
    except Exception as exc:  # noqa: BLE001
        print(f"SUBSCRIBE FAILED: {exc}")
        await client.disconnect()
        sys.exit(1)

    if args.write_cal:
        try:
            await client.write_gatt_char(RX_UUID, b"CAL", response=False)
            print("  -> sent CAL")
        except Exception as exc:  # noqa: BLE001
            print(f"  CAL write failed: {exc}")

    try:
        await asyncio.sleep(args.seconds)
    except KeyboardInterrupt:
        pass

    print(f"\nreceived {got['n']} message(s)")
    try:
        await client.stop_notify(TX_UUID)
    except Exception:
        pass
    await client.disconnect()
    print("disconnected. LINK OK" if got["n"] > 0 else "disconnected, but NO messages arrived")


if __name__ == "__main__":
    asyncio.run(main())
