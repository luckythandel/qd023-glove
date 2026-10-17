# QD023 Glove — PC Controller

ESP32 motion glove (flex sensors + MPU6050) → BLE → PC keyboard/mouse control.

Everything for this project lives in this folder.

```
qd023-glove/
├── firmware/
│   ├── qd023_ble_pc/          ← active sketch (what gets flashed)
│   │   └── qd023_ble_pc.ino
│   └── _archive/              ← older firmware versions
│       └── qd023_ble_pc.ino.orig
├── pc/
│   ├── pc_receiver.py         ← BLE receiver + key mapping + ASCII hand
│   └── pc_simple.py           ← numeric-only monitor (no keys pressed)
├── tools/
│   ├── flash.ps1              ← compile + upload with bootloader retry
│   ├── serial_watch.ps1       ← live serial log (no reset)
│   └── serial_cal.ps1         ← sends 'c' and captures a calibration run
├── docs/
│   ├── PINMAP.md              ← wiring, ADC pins, thresholds
│   ├── PROTOCOL.md            ← BLE + serial message formats
│   ├── ORIGINAL_README.md     ← first version of this README
│   └── reference/ACB_Gloves/  ← vendor tutorial library (source of truth)
└── logs/                      ← serial captures (not committed)
```

## Hardware

| Item | Value |
|---|---|
| Board | ESP32-WROOM-32D (ESP32-D0WD-V3) |
| Port | `COM13` @ `115200` |
| FQBN | `esp32:esp32:esp32` (upload speed `115200`) |
| IMU | MPU6050 @ `0x68`, SDA `G21`, SCL `G22` |
| Fingers | Thumb `G36`, Index `G39`, Middle `G34`, Ring `G35`, Pinky `G32` (all ADC1, 12-bit, 11 dB) |
| BLE name | `QD023-Glove` |

## Quick start

### 1. Flash

```powershell
& "C:\Users\lucky\arduino-cli\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 "C:\Users\lucky\qd023-glove\firmware\qd023_ble_pc"
```

Hold **BOOT**, tap **EN**, release **BOOT**, then:

```powershell
& "C:\Users\lucky\arduino-cli\arduino-cli.exe" upload -p COM13 --fqbn esp32:esp32:esp32:UploadSpeed=115200 "C:\Users\lucky\qd023-glove\firmware\qd023_ble_pc"
```

Or just use the helper (compiles, retries the bootloader window for you):

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\lucky\qd023-glove\tools\flash.ps1"
```

If upload complains the port is busy:

```powershell
Stop-Process -Name arduino-cli -Force -ErrorAction SilentlyContinue
```

### 2. Watch the glove

```powershell
& "C:\Users\lucky\arduino-cli\arduino-cli.exe" monitor -p COM13 -c baudrate=115200
```

Serial commands: `c` recalibrate · `d` raw pin dump (includes G36) · `h` help.

### 3. Run the PC side

Close the serial monitor first (only one program can hold COM13 — the BLE link is separate, so the receiver only needs Bluetooth on).

```powershell
# live hand viewer (GUI - see fingers curl, gesture, rotation, twist dial)
& "C:\Python312\python.exe" "C:\Users\lucky\qd023-glove\pc\hand_gui.py"

# prove the wireless link (scan, connect, print messages)
& "C:\Python312\python.exe" "C:\Users\lucky\qd023-glove\tools\ble_probe.py" --seconds 20

& "C:\Python312\python.exe" "C:\Users\lucky\qd023-glove\pc\pc_receiver.py" --dry-run   # log only
& "C:\Python312\python.exe" "C:\Users\lucky\qd023-glove\pc\pc_receiver.py"             # presentation keys
& "C:\Python312\python.exe" "C:\Users\lucky\qd023-glove\pc\pc_receiver.py" --game      # game keys
& "C:\Python312\python.exe" "C:\Users\lucky\qd023-glove\pc\pc_simple.py"               # numbers only
```

No OS pairing step is needed: the receiver speaks BLE GATT through `bleak`
(device `QD023-Glove`, address `68:09:47:36:21:92`). If the glove is already
connected elsewhere it stops advertising, so the receiver falls back to that
known address. It also **reconnects automatically** every 3 s if the link drops
(e.g. the glove resets after a reflash).

## Calibration (runs on every boot)

1. **Step 0** — hand flat, fingers straight, 3 s → records the OPEN baseline.
2. **Step 1–5** — bend **only** the named finger to a **comfortable** position and hold ~2 s.
3. **Rotation** — palm toward you 3 s, then physically turn the glove **over** 3 s.

Per finger it prints either `OK` (with travel + threshold) or `NO_TRAVEL` / `WEAK`
(previous values kept, sensor marked inactive).

The threshold is placed at **32 % of travel away from OPEN** — the same idea as the
tutorial's fixed `> 200` rule, but measured for your own fingers, so a light bend
already registers instead of needing a full fist.

## Gesture → key map

| Gesture | Presentation | Game |
|---|---|---|
| `OPEN_PALM` | `F5` start slideshow | `F5` start/menu |
| `FIST` | `ESC` | `Space` jump/attack |
| `INDEX_UP` | `Right` next | `E` interact |
| `PEACE` | `Left` prev | `Q` secondary |
| `THUMBS_UP` | `B` blank | `B` emote |
| `PINKY_UP` | `M` mute | `M` map |
| `ROT:PALM_VIEW` | `P` | `P` |
| `ROT:BACK_VIEW` | `O` | `O` |
| `FINGER:<name>:BENT` | per-finger key | same |

## Troubleshooting

| Symptom | Fix |
|---|---|
| `Wrong boot mode detected (0x13)` | Hold BOOT, tap EN, release BOOT, retry upload |
| Port busy / access denied | Close Arduino IDE monitor, `Stop-Process -Name arduino-cli -Force` |
| Finger never registers | run `d`, bend that finger, check which column moves |
| Everything reads 0 except one | that finger rests at 0 — it is fine, bend it and re-check |
| `dot=100.9` in rotation cal | templates too similar — actually rotate the hand palm→back |
| Receiver finds no device | Bluetooth on, glove advertising (LED blink), name `QD023-Glove` |

See [PINMAP.md](docs/PINMAP.md) and [PROTOCOL.md](docs/PROTOCOL.md) for details.
