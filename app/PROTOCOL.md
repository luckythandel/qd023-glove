# Glove Bridge ↔ React App WebSocket Protocol

Bridge listens on `ws://127.0.0.1:8787`. One JSON object per message, no envelopes.

## Bend model

- Glove streams `FINGERS:t,i,m,r,p` where each value is 0–100 % flex travel.
- **Bend rule (per-finger, configurable):** a finger counts as bent iff pct ≥ its `on` threshold and releases at ≤ its `off`. Defaults: **thumb `on` 56 / `off` 30**; **index/middle/ring/pinky `on` 46 / `off` 18**.
- Thresholds persist in `config.json` → `thresholds` (see below) **and** in the glove's NVS (`glove` namespace). On connect the glove's copy wins; see Threshold sync at the end of this document. The web Settings panel exposes the `on` value as a slider (15–95 %); `off` is derived as `max(5, on − 26)` so the hysteresis band tracks the slider.
- Thumb is free (ignored) in most gestures unless the table says otherwise.
- Firmware `FINGER:` edges are trusted for index/middle/ring/pinky; the thumb ignores those edges (firmware threshold is ~32 %) and follows its `on` `FINGERS:` rule instead.

## Bridge → Web

| type | fields | when |
|------|--------|------|
| `connection` | `connected: bool`, `device: string` | BLE connect/disconnect |
| `fingers` | `pct: [t,i,m,r,p]` (numbers), `bent: [bool×5]`, `twist: number \| null` (absolute wrist angle, deg) | every glove `FINGERS:` / `FINGER:` / `TWIST:` line |
| `pose` | `name: string \| null` | debounced pose change |
| `action` | `action: string`, `gesture: string` | when an action fires |
| `status` | `cal: string` (e.g. `OK` / `NEED_CALIBRATION`), `text: string` | cal/pose text from glove |
| `config` | `bindings`, `thresholds`, `custom` (shapes in Config file below) | on WS connect, after `request_config`, and after `set_config` |
| `log` | `level: "info"\|"warn"\|"error"`, `msg: string` | optional diagnostics |

## Web → Bridge

| type | fields | effect |
|------|--------|--------|
| `set_config` | **any subset** of `bindings`, `thresholds`, `custom` | merge over current → validate → persist full file; broadcast full `config` echo (old bindings-only messages still accepted) |
| `request_config` | — | reply with `config` |
| `ping` | — | reply `{"type":"pong"}` |

## Config file (`config.json`, bridge-owned)

```json
{
  "bindings": { "FIST": "play_pause", "...": "..." },
  "thresholds": {
    "thumb":  { "on": 56, "off": 30 },
    "index":  { "on": 46, "off": 18 },
    "middle": { "on": 46, "off": 18 },
    "ring":   { "on": 46, "off": 18 },
    "pinky":  { "on": 46, "off": 18 }
  },
  "custom": [
    { "id": "c1", "name": "Gun", "pattern": ["B", "B", "E", "X", "X"], "action": "mouse_right", "rot": "none" }
  ]
}
```

- Finger order everywhere: **thumb, index, middle, ring, pinky** (indices 0–4).
- Validation: `on` clamped **15–95**, `off` clamped **5..on−5**; invalid entries fall back to the defaults above. `custom` capped at **12**; `id` ≤ 32 chars (bridge generates one when missing/empty); `name` ≤ 24 chars (shown as the pose name); `pattern` must be exactly 5 entries from `{B, E, X}`; unknown `action` → `none`; `rot` must be `none` / `cw` / `ccw` (missing or unknown → `none`).
- The bridge rewrites the full document on every `set_config`; partial web messages merge over current state.

## GestureId (detection per bend model above)

| id | condition (thumb free unless noted) |
|----|-------------------------------------|
| `FIST` | all five fingers bent |
| `INDEX_UP` | index extended, middle+ring+pinky bent, thumb free |
| `PEACE` | index+middle extended, ring+pinky bent, thumb free |
| `THUMBS_UP` | thumb extended, all four fingers bent |
| `THUMB_BENT` | thumb bent, all four fingers extended |
| `MIDDLE_UP` | middle extended, index+ring+pinky bent, thumb free |
| `PINKY_UP` | pinky extended, index+middle+ring bent, thumb free |
| `OPEN_PALM` | none bent |
| `MID_RING` | middle+ring bent, index+pinky extended, thumb free (twist mode) |

## Custom gestures (`config.json` → `custom`)

Each entry defines its own pose **and** its action:

- `pattern` = 5 entries in **thumb, index, middle, ring, pinky** order: `B` bent · `E` extended · `X` don't-care.
- `rot` (optional, default `none`): wrist-rotation gate. `none` fires on the pattern alone; `cw` / `ccw` additionally require a wrist turn of at least **45°** (clockwise / counter-clockwise) *after* the pattern has matched — bend first, then rotate. The reference angle is latched the moment the pattern first matches; it re-latches after the pattern releases, so a later rotation from a new baseline can re-fire the gesture. Missing/unknown values normalize to `none`.
- Classification order: the 9 built-in poses above are checked **first** (built-ins win), then custom entries in list order — **first match wins**. A custom pattern that mirrors a built-in combo is shadowed by that built-in.
- A matched custom pose fires through the same 150 ms stability + cooldown/repeat machinery as built-ins; the `pose` broadcast carries the custom `name`.
- Built/edited in the web Settings panel ("Custom Gestures"); persisted in `config.json`.

## ActionId

PC media keys (pyautogui): `play_pause`, `next`, `prev`, `volume_up`, `volume_down`, `mute`, `stop`, `none`

Mouse actions (pyautogui, bindable like media keys):

| action | behavior |
|--------|----------|
| `mouse_left` / `mouse_right` / `mouse_up` / `mouse_down` | cursor nudge **60 px**, re-fires every **80 ms** while pose held |
| `mouse_click` / `mouse_rclick` | left / right click, normal **400 ms** cooldown |

## Default bindings

```
FIST       → play_pause
INDEX_UP   → next
PEACE      → prev
THUMBS_UP  → none
THUMB_BENT → none
MIDDLE_UP  → volume_down
PINKY_UP   → mute
OPEN_PALM  → none
MID_RING   → none (wrist twist sets volume directly, see below)
```

## Wrist twist (absolute volume)

While `MID_RING` is the active pose, wrist rotation sets **absolute** system volume (pycaw):

- Baseline captured from the `TWIST:` angle at pose entry, together with the current volume % (`v0`).
- **+180° clockwise → volume 100 %, −180° counter-clockwise → volume 0 %**, clamped, linear in between: `target = v0 + delta × 100/180`.
- Writes throttled to one per **60 ms**; an `action` toast (`volume_up` / `volume_down`) flashes every **10 %** of movement — no media keys are sent in this mode.
- Baseline resets whenever the active pose changes.
- **Hold-freeze**: volume changes only while the wrist is actually twisting. Two consecutive samples at ≤ **58 °/s** freeze the mapping (baseline then tracks the still wrist so gyro drift can't move volume); two consecutive samples at ≥ **60 °/s** resume it, re-anchoring `v0` on the current volume — so holding the wrist steady after a twist keeps the volume exactly where it was left.
- **Fallback** (pycaw unavailable): `volume_up`/`volume_down` key steps, one per **10°**, re-firing at most every **250 ms**.

## Debounce / repeat

- Pose must hold **stable 150 ms** before it becomes the active pose.
- Action fires on **rising edge** of a bound pose (not while held).
- Cooldown per action: **400 ms** generally; `volume_up`/`volume_down` may re-fire every **250 ms** while pose held (repeat); `mouse_left/right/up/down` re-fire every **80 ms** while held.
- Unbound (`none`) poses never fire.

## Glove BLE (bridge side only)

- Name: `QD023-Glove`, suffixed with the active control mode:
  `-W` (Windows), `-L` (LED), `-A` (Android), `-M` (Music)
- Manufacturer data: company id `0xFFFF`, one byte = mode. This byte is
  authoritative; the name can be a stale GAP cache on Windows/Android.
- NUS service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- TX (notify) `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- RX (write) `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- Lines newline-terminated; `FINGERS:0,0,0,0,0` every `sendEveryMs` (default 600 ms) after cal.

### Music mode frames (firmware 3.4+)

Sent only while the glove is in music mode (`ctl = 3`).

| direction | line | meaning |
|---|---|---|
| glove → client | `MUS:<mask>` | finger bitmask, bit 0 = thumb … bit 4 = pinky; sent when it changes plus a 400 ms heartbeat |
| glove → client | `TWIST:<deg>` | unchanged; the client maps it to pitch bend |
| client → glove | `MOD:<0..3>` | set control mode (disconnects the current central) |
| client → glove | `GETMODE` | reply `MOD:<n>` with the current mode |

Bridge side: `MUS:` is mirrored to WebSocket clients as
`{"type":"music","mask":n,"notes":["C5",...]}`.

### Threshold sync

The glove's NVS copy of the percent bands is the shared source of truth: the bridge pulls it on connect, so a value saved on the on-board portal wins over `config.json`.

| direction | line | meaning |
|---|---|---|
| bridge → glove | `GETCFG` | request the stored config |
| glove → bridge | `THR:t,i,m,r,p` | `on` bands |
| glove → bridge | `THO:t,i,m,r,p` | `off` bands |
| glove → bridge | `TWD:pause,resume` | twist hold-freeze gates, deg/s |
| bridge → glove | `SO:t,i,m,r,p` | write `on` bands (persisted to NVS) |
| bridge → glove | `SF:t,i,m,r,p` | write `off` bands (persisted to NVS) |
| bridge → glove | `ST:pause,resume` | write twist gates (persisted to NVS) |
| glove → bridge | `OK:SO` / `OK:SF` / `OK:ST` | write acknowledged |

All frames stay ≤ 20 bytes so they fit the default ATT payload without an MTU negotiation.

