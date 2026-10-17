"""
QD023 Glove -> PC receiver (BLE NUS + keyboard control)
Requires: pip install bleak pyautogui
Usage:
  python pc_receiver.py --dry-run        # print gestures, no keypress (safe test)
  python pc_receiver.py                  # live control PowerPoint/Chrome/game
  python pc_receiver.py --game           # game mapping (WASD/Space)
Device: QD023-Glove, Service 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
"""
import argparse, asyncio, sys

SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # glove -> PC (notify)
NAME = "QD023-Glove"

# Presentation mode: PowerPoint / Chrome / VLC friendly
PRESENT_MAP = {
    "OPEN_PALM": ("f5", "Start slideshow"),
    "FIST": ("esc", "End / back"),
    "INDEX_UP": ("right", "Next slide"),
    "PEACE": ("left", "Prev slide"),
    "THUMBS_UP": ("b", "Blank screen"),
    "PINKY_UP": ("m", "Mute/unmute"),
}
# Per-finger actions: each finger bend/extend is independently bindable
FINGER_MAP = {
    "Thumb:BENT": ("t", "Thumb bent"),
    "Thumb:EXT": (None, "Thumb extended"),
    "Index:BENT": ("right", "Index bent = next"),
    "Index:EXT": ("left", "Index extended = prev"),
    "Middle:BENT": ("down", "Middle bent"),
    "Middle:EXT": ("up", "Middle extended"),
    "Ring:BENT": ("space", "Ring bent = select"),
    "Ring:EXT": (None, "Ring extended"),
    "Pinky:BENT": ("m", "Pinky bent = mute"),
    "Pinky:EXT": (None, "Pinky extended"),
}
# Palm orientation: watching palm vs back of hand
ROT_MAP = {
    "PALM_VIEW": ("p", "Palm toward you (watching palm)"),
    "BACK_VIEW": ("o", "Back of hand toward you"),
}
# Game mode: tiltIdeally WASD, buttons as actions
GAME_MAP = {
    "OPEN_PALM": ("f5", "Start / menu"),
    "FIST": ("space", "Jump / attack"),
    "INDEX_UP": ("e", "Interact"),
    "PEACE": ("q", "Secondary"),
    "THUMBS_UP": ("b", "Emote"),
    "PINKY_UP": ("m", "Map"),
}

def act_for(gesture, game=False):
    m = GAME_MAP if game else PRESENT_MAP
    return m.get(gesture, (None, "no binding (ignored)"))

import os as _os, time as _time, sys as _sys

_hand = {"pcts": [0, 0, 0, 0, 0], "bent": [False]*5, "gesture": "-", "rot": "-", "event": "waiting for glove...", "t": 0.0}

ARTS = {
"OPEN_PALM": [
"         __    __    __    __",
"        |  |  |  |  |  |  |  |",
"        |  |  |  |  |  |  |  |",
"        |  |  |  |  |  |  |  |",
"         --    --    --    --",
"      __ ______________________",
"     |  |                      |",
"     |  |                      |",
"      -- \\                    /",
"          |                  |",
"           ------------------",
],
"FIST": [
"      _________________",
"     |  _  _  _  _  _  |",
"     | | || || || || | |",
"      -- -- -- -- -- --",
"     |                 |",
"      \\               /",
"       ---------------",
],
"INDEX_UP": [
"               __",
"              |  |",
"              |  |",
"              |  |",
"               --",
"      _________________",
"     |  _     _  _  _  |",
"     | | |  | || || | |",
"      -- -- -- -- -- --",
"     |                 |",
"      \\               /",
],
"PEACE": [
"        __    __",
"       |  |  |  |",
"       |  |  |  |",
"       |  |  |  |",
"        --    --",
"      _________________",
"     |  _  _     _  _  |",
"     | | || |   | || | |",
"      -- -- -- -- -- --",
"     |                 |",
"      \\               /",
],
"THUMBS_UP": [
"      __",
"     |  |",
"     |  |",
"     |  |",
"      -- _________________",
"     |  |  _  _  _  _  |",
"     |  | | || || || | |",
"      -- -- -- -- -- --",
"     |                 |",
"      \\               /",
],
}

def draw_hand(animate=True):
    pcts, bent, gesture, rot, event = _hand["pcts"], _hand["bent"], _hand["gesture"], _hand["rot"], _hand["event"]
    art = ARTS.get(gesture)
    if art is None:
        marks = "".join("v" if b else "| " for b in bent).strip()
        art = [
        "   T   I   M   R   P",
        "   " + "   ".join(("v" if b else "|") for b in bent),
        "   " + "   ".join(str(p) for p in pcts),
        " " + "-" * 25,
        ]
    palm = "( PALM > you )" if rot == "PALM_VIEW" else ("[ BACK > you ]" if rot == "BACK_VIEW" else "[ hand ]")
    state = "T:{} I:{} M:{} R:{} P:{}".format(*("BENT" if b else "EXT" for b in bent))
    rows = list(art) + [" " * 4 + palm, f" {gesture:12s} {rot:10s}", f" {state}", f" {event[:42]:42s}"]
    out = "\n".join(rows)
    if animate:
        _os.system("cls" if _os.name == "nt" else "clear")
        print(out, flush=True)
    else:
        print(out, flush=True)

def note_event(msg, animate=True):
    _hand["event"] = msg
    _hand["t"] = _time.time()
    draw_hand(animate=animate)

async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="print only, no keypress")
    ap.add_argument("--game", action="store_true", help="use game mapping")
    ap.add_argument("--no-animate", action="store_true", help="disable ASCII hand, plain log lines")
    ap.add_argument("--name", default=NAME)
    args = ap.parse_args()
    animate = not args.no_animate

    try:
        from bleak import BleakScanner, BleakClient
    except ImportError:
        print("pip install bleak pyautogui"); sys.exit(1)

    pyautogui = None
    if not args.dry_run:
        try:
            import pyautogui as pg
            pyautogui = pg
            pyautogui.FAILSAFE = True
            print("pyautogui live (move mouse to corner to abort).")
        except ImportError:
            print("pip install pyautogui"); sys.exit(1)
    else:
        print("DRY-RUN: no keys will be pressed.")

    print(f"Scanning for {args.name} (BLE on, glove powered, flashed with qd023_ble_pc.ino)...")
    dev = await BleakScanner.find_device_by_name(args.name, timeout=20)
    if not dev:
        print(f"Not found. Check: 1) glove ON + flashed, 2) PC Bluetooth ON, 3) close ACECode/Arduino monitor holding COM13.")
        sys.exit(2)
    print(f"Found {dev.address}, connecting...")

    def on_rx(sender, data):
        try:
            txt = bytes(data).decode(errors="ignore").strip()
        except Exception:
            return
        for line in txt.splitlines():
            line = line.strip()
            if not line:
                continue
            if line.startswith("GESTURE:"):
                g = line.split(":", 1)[1].strip()
                _hand["gesture"] = g
                key, desc = act_for(g, args.game)
                tag = "[GAME]" if args.game else "[PRESENT]"
                msg = f"{tag} {g} -> press '{key}' ({desc})" if (key and not args.dry_run and pyautogui) else f"{tag} {g} -> [{key or '---'}] {desc}" + (" (dry-run)" if args.dry_run else "")
                if key and not args.dry_run and pyautogui:
                    pyautogui.press(key)
                note_event(msg, animate=animate)
            elif line.startswith("FINGER:"):
                parts = line.split(":")
                key, desc = (None, "no binding")
                if len(parts) == 3:
                    k = parts[1].strip() + ":" + parts[2].strip()
                    key, desc = FINGER_MAP.get(k, (None, "no binding"))
                    nm = parts[1].strip().lower()
                    idx = {"thumb": 0, "index": 1, "middle": 2, "ring": 3, "pinky": 4}.get(nm)
                    if idx is not None:
                        _hand["bent"][idx] = (parts[2].strip() == "BENT")
                msg = f"[FINGER] {line} -> press '{key}' ({desc})" if (key and not args.dry_run and pyautogui) else f"[FINGER] {line} -> [{key or '---'}] {desc}" + (" (dry-run)" if args.dry_run else "")
                if key and not args.dry_run and pyautogui:
                    pyautogui.press(key)
                note_event(msg, animate=animate)
            elif line.startswith("ROT:"):
                r = line.split(":", 1)[1].strip()
                _hand["rot"] = r
                key, desc = ROT_MAP.get(r, (None, "no binding"))
                msg = f"[ROT] {r} -> press '{key}' ({desc})" if (key and not args.dry_run and pyautogui) else f"[ROT] {r} -> [{key or '---'}] {desc}" + (" (dry-run)" if args.dry_run else "")
                if key and not args.dry_run and pyautogui:
                    pyautogui.press(key)
                note_event(msg, animate=animate)
            elif line.startswith("FINGERS:"):
                try:
                    vals = [max(0, min(100, int(x))) for x in line.split(":", 1)[1].split(",")]
                    if len(vals) == 5:
                        _hand["pcts"] = vals
                        draw_hand(animate=animate)
                except Exception:
                    pass
            elif line == "READY":
                print("Glove ready. Show gestures!")
            else:
                print("BLE:", line)

    async with BleakClient(dev) as cli:
        print("Connected. Showing gestures - Ctrl+C to quit. Move mouse to corner to abort pyautogui.")
        await cli.start_notify(TX_UUID, on_rx)
        while True:
            await asyncio.sleep(1)

if __name__ == "__main__":
    asyncio.run(main())
