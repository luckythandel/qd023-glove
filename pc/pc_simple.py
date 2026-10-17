"""QD023 numeric monitor: finger values + hand state, no art, no keypress."""
import argparse, asyncio, sys

SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
NAMES = ["Thumb", "Index", "Middle", "Ring", "Pinky"]

async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="QD023-Glove")
    args = ap.parse_args()
    from bleak import BleakScanner, BleakClient
    state = {"pcts": [0]*5, "bent": [False]*5, "gesture": "-", "rot": "-"}
    print(f"Scanning for {args.name}...")
    dev = await BleakScanner.find_device_by_name(args.name, timeout=20)
    if not dev:
        print("Not found. Glove ON + Bluetooth ON + disconnect phone.")
        sys.exit(2)
    print(f"Connected to {dev.address}. Move fingers, Ctrl+C quits.\n")

    def show():
        parts = []
        for i, n in enumerate(NAMES):
            s = "BENT" if state["bent"][i] else "EXT"
            parts.append(f"{n}: {state['pcts'][i]:3d} ({s})")
        print(" | ".join(parts) + f"  || Hand: {state['gesture']}  |  Palm: {state['rot']}", flush=True)

    def on_rx(sender, data):
        try:
            txt = bytes(data).decode(errors="ignore")
        except Exception:
            return
        for line in txt.splitlines():
            line = line.strip()
            if line.startswith("FINGERS:"):
                try:
                    v = [max(0, min(100, int(x))) for x in line.split(":", 1)[1].split(",")]
                    if len(v) == 5:
                        state["pcts"] = v
                        show()
                except Exception:
                    pass
            elif line.startswith("GESTURE:"):
                state["gesture"] = line.split(":", 1)[1].strip()
                show()
            elif line.startswith("FINGER:"):
                p = line.split(":")
                if len(p) == 3:
                    nm, st = p[1].strip().lower(), p[2].strip() == "BENT"
                    idx = {"thumb": 0, "index": 1, "middle": 2, "ring": 3, "pinky": 4}.get(nm)
                    if idx is not None:
                        state["bent"][idx] = st
            elif line.startswith("ROT:"):
                state["rot"] = line.split(":", 1)[1].strip()
                show()

    async with BleakClient(dev) as cli:
        await cli.start_notify(TX_UUID, on_rx)
        while True:
            await asyncio.sleep(1)

if __name__ == "__main__":
    asyncio.run(main())
