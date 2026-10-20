# QD023 Glove — PC Media Control

React UI + Python BLE bridge. The glove streams finger flex % over BLE Nordic UART; the bridge classifies poses with **per-finger bend thresholds** (defaults: thumb **56 %**, others **46 %** — adjustable live in the app), fires Windows media keys and **mouse actions** (cursor nudges + clicks), runs **custom gestures you define yourself**, and handles absolute volume via pycaw for the twist pose — streaming live state to the browser over WebSocket.

Protocol: [`PROTOCOL.md`](PROTOCOL.md) (source of truth for bridge + web).

## Run (two terminals)

**1 — Bridge** (BLE + media keys + WS server on `ws://127.0.0.1:8787`)

```powershell
C:\Python312\python.exe C:\Users\lucky\qd023-glove\app\bridge\glove_bridge.py
```

Optional flags:

```powershell
# fake glove stream — no hardware needed (good for trying the UI)
C:\Python312\python.exe C:\Users\lucky\qd023-glove\app\bridge\glove_bridge.py --simulate

# custom port / device name
C:\Python312\python.exe C:\Users\lucky\qd023-glove\app\bridge\glove_bridge.py --port 8787 --device-name QD023-Glove --verbose
```

**2 — Web app**

```powershell
cd C:\Users\lucky\qd023-glove\app\web
npm run dev
```

Open **http://localhost:5173**. The page auto-reconnects to the bridge.

> The glove must be powered on and calibrated (flat → fist) before finger data flows. If the bridge can't find it, check the glove is advertising and no other app (e.g. `hand_gui.py`) is holding the BLE link.

## Default gestures (all rebindable in the app)

A finger counts as bent when it passes its **per-finger threshold** (defaults: **thumb ≥ 56 %**, index/middle/ring/pinky **≥ 46 %** — tune each with the **Finger Sensitivity** sliders, e.g. accept your index at **40 %**). Hold a pose ~150 ms to fire; 400 ms cooldown (volume and mouse repeats fire while held).

| Gesture | Condition (thumb free unless noted) | Action |
|---------|--------------------------------------|--------|
| **Fist** | all five fingers bent | Play / Pause |
| **Index Up** | index extended, middle+ring+pinky bent | Next track |
| **Peace** | index+middle extended, ring+pinky bent | Previous track |
| **Thumbs Up** | thumb extended, all four fingers bent | none (unbound) |
| **Thumb Bent** | thumb bent (≥ 56 %), all four fingers extended | none (unbound) |
| **Middle Up** | middle extended, index+ring+pinky bent | Volume down |
| **Pinky Up** | pinky extended, index+middle+ring bent | Mute |
| **Open Palm** | none bent | none (unbound) |
| **Mid + Ring** | middle+ring bent, index+pinky extended | twist wrist → absolute volume |

**Volume by wrist twist:** fold your **middle and ring fingers** (`Mid + Ring`, index+pinky stay out), then rotate your wrist — a **half turn clockwise** from where you started sets volume to **100 %**, a **half turn counter-clockwise** to **0 %**, linear in between (absolute set via pycaw, one write per 60 ms; the UI flashes a volume toast every 10 %).

**Custom gestures:** Settings → **Custom Gestures** lets you add your own poses — name, one state per finger (Any / Bent / Extended; *Any* = don't care), an action, and an optional wrist-rotation gate (**Clockwise / Counter-clockwise ≥45°** — bend the pattern first, then rotate your wrist to fire). Max 12; built-in gestures take priority if a pattern matches them. Custom poses appear in the hand view just like built-ins.

**Mouse control:** the Gesture → Action dropdowns also include **Mouse Left/Right/Up/Down** (60 px cursor nudge, repeats every 80 ms while held) and **Left Click / Right Click** — bind them to any built-in or custom gesture.

## Layout

```
app/
  PROTOCOL.md          WS contract shared by bridge + web
  bridge/
    glove_bridge.py    BLE → gesture engine → media keys → WS
    config.json        auto-created; persisted bindings + thresholds + custom gestures
  web/
    src/               Vite + React + TS UI (animated SVG hand, settings)
    package.json
```

## Notes

- **Simulate toggle** (settings panel): drives the hand locally when the bridge is offline — handy for testing animations without the glove.
- Settings (bindings, sensitivity thresholds, custom gestures) sync to the bridge via `set_config` and persist in `app/bridge/config.json` (plus localStorage as offline fallback).
- Firmware `FINGERS:pct` values come from the two-pose calibration; if a finger reads `INACTIVE`, press `c` on serial and recalibrate.
