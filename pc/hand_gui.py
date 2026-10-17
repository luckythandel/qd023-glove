"""
QD023-Glove - Live Hand Viewer (GUI)

Shows, in real time over BLE:
  - a hand drawing where each finger curls/extends with the live 0-100% value
    and changes color (green = straight, red = fully bent)
  - the current gesture and palm orientation
  - wrist twist as a dial (CW/CCW + degrees)
  - a scrolling event log

No extra dependencies: Tkinter (stdlib) + bleak (already installed).

Usage:
    python hand_gui.py
    python hand_gui.py --name QD023-Glove
    python hand_gui.py --address 68:09:47:36:21:92
"""
import argparse
import asyncio
import math
import queue
import threading

import tkinter as tk
import tkinter.font as tkfont

NAME = "QD023-Glove"
KNOWN_ADDRESS = "68:09:47:36:21:92"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # glove -> PC (notify)

# finger display order and colors
FINGERS = [
    ("Thumb",  0, "#f4a259"),
    ("Index",  1, "#5aa9e6"),
    ("Middle", 2, "#5aa9e6"),
    ("Ring",   3, "#5aa9e6"),
    ("Pinky",  4, "#5aa9e6"),
]

BG = "#14161a"
PALM = "#2b3040"
TXT = "#d7dae2"
MUTED = "#8a92a6"


class State:
    def __init__(self):
        self.pcts = [0, 0, 0, 0, 0]     # 0 = straight, 100 = fully bent
        self.bent = [False] * 5
        self.gesture = "-"
        self.rot = "-"
        self.twist = 0.0                # degrees, + = clockwise
        self.twist_rate = 0.0


def parse_packet(data: bytearray, q):
    try:
        txt = bytes(data).decode(errors="ignore")
    except Exception:
        return
    for line in txt.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("FINGERS:"):
            try:
                vals = [max(0, min(100, int(x))) for x in line.split(":", 1)[1].split(",")]
                if len(vals) == 5:
                    q.put({"type": "fingers", "pcts": vals})
            except Exception:
                pass
        elif line.startswith("FINGER:"):
            parts = line.split(":")
            if len(parts) == 3:
                q.put({"type": "finger", "name": parts[1].strip(),
                       "state": parts[2].strip()})
        elif line.startswith("GESTURE:"):
            q.put({"type": "gesture", "value": line.split(":", 1)[1].strip()})
        elif line.startswith("ROT:"):
            q.put({"type": "rot", "value": line.split(":", 1)[1].strip()})
        elif line.startswith("TWIST:"):
            try:
                q.put({"type": "twist", "value": int(line.split(":", 1)[1].strip())})
            except Exception:
                pass
        else:
            q.put({"type": "log", "value": line})


async def ble_main(q, name, address):
    from bleak import BleakClient, BleakScanner
    while True:
        try:
            dev = None
            if not address:
                dev = await BleakScanner.find_device_by_name(name, timeout=15)
            addr = address or (dev.address if dev else None)
            if not addr:
                q.put({"type": "log", "value": f"'{name}' not found - retrying"})
                await asyncio.sleep(3)
                continue
            q.put({"type": "status", "value": f"connecting {addr} ..."})

            def on_rx(_s, d):
                parse_packet(d, q)

            async with BleakClient(addr, timeout=20) as cli:
                q.put({"type": "status", "value": f"connected {addr}"})
                await cli.start_notify(TX_UUID, on_rx)
                while cli.is_connected:
                    await asyncio.sleep(1)
                q.put({"type": "status", "value": "disconnected - reconnecting"})
        except Exception as exc:  # noqa: BLE001
            q.put({"type": "status", "value": f"link error: {exc}"})
        await asyncio.sleep(3)


class HandGUI:
    def __init__(self, root, name, address):
        self.root = root
        self.q = queue.Queue()
        self.state = State()
        self.log_lines = []
        self.status = "starting..."

        root.title("QD023 Glove - Live Hand")
        root.configure(bg=BG)

        self.canvas = tk.Canvas(root, width=520, height=560, bg=BG, highlightthickness=0)
        self.canvas.pack(side=tk.LEFT, padx=(10, 0), pady=10)

        panel = tk.Frame(root, bg=BG, width=300)
        panel.pack(side=tk.LEFT, fill=tk.Y, padx=12, pady=12)
        panel.pack_propagate(False)

        self.var_gesture = tk.StringVar(value="-")
        self.var_rot = tk.StringVar(value="-")
        self.var_twist = tk.StringVar(value="0°")
        self.var_dir = tk.StringVar(value="—")
        self.var_status = tk.StringVar(value=self.status)

        def label(parent, text, var, size=11, color=TXT):
            f = tk.Label(parent, text=text, font=("Segoe UI", 10, "bold"),
                         bg=BG, fg=MUTED, anchor="w")
            f.pack(fill=tk.X, pady=(12, 0))
            v = tk.Label(parent, textvariable=var, font=("Segoe UI", size, "bold"),
                         bg=BG, fg=color, anchor="w")
            v.pack(fill=tk.X)
            return v

        label(panel, "GESTURE", self.var_gesture, 20, "#7fd1a8")
        label(panel, "PALM / ROTATION", self.var_rot, 15)
        label(panel, "WRIST TWIST", self.var_twist, 22, "#f4a259")
        tk.Label(panel, textvariable=self.var_dir, font=("Segoe UI", 11),
                 bg=BG, fg=MUTED, anchor="w").pack(fill=tk.X)

        tk.Label(panel, text="LOG", font=("Segoe UI", 10, "bold"),
                 bg=BG, fg=MUTED, anchor="w").pack(fill=tk.X, pady=(18, 0))
        self.logbox = tk.Listbox(panel, bg="#1c1f26", fg=MUTED, bd=0,
                                 highlightthickness=0, font=("Consolas", 9))
        self.logbox.pack(fill=tk.BOTH, expand=True, pady=(4, 0))

        tk.Label(panel, textvariable=self.var_status, font=("Segoe UI", 9),
                 bg=BG, fg="#5f6b85", anchor="w").pack(fill=tk.X, pady=(8, 0))

        self.draw()
        self.root.after(50, self.poll)

        threading.Thread(target=lambda: asyncio.run(ble_main(self.q, name, address)),
                         daemon=True).start()

    # ---- state handling -------------------------------------------------
    def apply(self, m):
        t = m["type"]
        if t == "fingers":
            self.state.pcts = m["pcts"]
            self.state.bent = [p >= 50 for p in m["pcts"]]
        elif t == "finger":
            idx = {"thumb": 0, "index": 1, "middle": 2, "ring": 3, "pinky": 4}.get(m["name"].lower())
            if idx is not None:
                self.state.bent[idx] = (m["state"] == "BENT")
        elif t == "gesture":
            self.state.gesture = m["value"]
            self.log(m["value"])
        elif t == "rot":
            self.state.rot = m["value"]
        elif t == "twist":
            self.state.twist = m["value"]
        elif t == "status":
            self.status = m["value"]
            self.var_status.set(self.status)
        elif t == "log":
            self.log(m["value"])

    def log(self, s):
        self.log_lines.append(s)
        self.log_lines = self.log_lines[-40:]
        self.logbox.delete(0, tk.END)
        for line in self.log_lines:
            self.logbox.insert(tk.END, line)
        self.logbox.yview_moveto(1.0)

    def poll(self):
        try:
            while True:
                self.apply(self.q.get_nowait())
        except queue.Empty:
            pass
        self.draw()
        self.root.after(50, self.poll)

    # ---- drawing --------------------------------------------------------
    def draw(self):
        c = self.canvas
        c.delete("all")
        s = self.state

        # palm
        c.create_oval(95, 300, 385, 500, fill=PALM, outline="")

        # finger geometry: base y on palm top edge, tip rises as it straightens
        base_y = 330
        max_len = 175
        xs = [100, 170, 230, 290, 360]
        names = ["T", "I", "M", "R", "P"]

        for i, (name, idx, color) in enumerate(FINGERS):
            pct = s.pcts[idx]
            # 0% straight = tall; 100% bent = short stub
            tip_y = base_y - int(max_len * (1.0 - pct / 100.0))
            if tip_y < 40:
                tip_y = 40
            # color from green (straight) to red (bent)
            rgb = self.lerp_color((95, 205, 130), (235, 92, 92), pct / 100.0)
            fill = "#%02x%02x%02x" % rgb

            c.create_line(xs[i], base_y, xs[i], tip_y, width=30,
                          capstyle=tk.ROUND, fill=fill)
            c.create_oval(xs[i] - 16, tip_y - 20, xs[i] + 16, tip_y + 20,
                          fill=fill, outline="")
            c.create_text(xs[i], tip_y - 34, text=name,
                          font=("Segoe UI", 10, "bold"), fill=TXT)
            c.create_text(xs[i], base_y + 24, text=f"{pct}%",
                          font=("Segoe UI", 9), fill=MUTED)

        # wrist
        c.create_line(240, 500, 240, 560, width=26, capstyle=tk.ROUND, fill=PALM)

        # twist dial (top-right of canvas)
        cx, cy, r = 440, 90, 52
        c.create_oval(cx - r, cy - r, cx + r, cy + r, outline="#2f3542", width=6)
        ang = max(-180, min(180, s.twist))
        rad = (ang - 90) * math.pi / 180.0
        nx = cx + (r - 14) * math.cos(rad)
        ny = cy + (r - 14) * math.sin(rad)
        c.create_line(cx, cy, nx, ny, width=4, fill="#f4a259", capstyle=tk.ROUND)
        c.create_oval(cx - 5, cy - 5, cx + 5, cy + 5, fill="#f4a259", outline="")
        c.create_text(cx, cy + r + 18, text="twist", font=("Segoe UI", 9), fill=MUTED)

        # readouts
        self.var_gesture.set(s.gesture)
        self.var_rot.set(s.rot)
        self.var_twist.set(f"{int(s.twist):+d}°")
        self.var_dir.set("clockwise" if s.twist > 0 else ("anticlockwise" if s.twist < 0 else "neutral"))

    @staticmethod
    def lerp_color(a, b, t):
        t = max(0.0, min(1.0, t))
        return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default=NAME)
    ap.add_argument("--address", default="")
    args = ap.parse_args()

    root = tk.Tk()
    HandGUI(root, args.name, args.address or None)
    root.mainloop()


if __name__ == "__main__":
    main()
