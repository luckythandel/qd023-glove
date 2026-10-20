# QD023 Glove — Universal BLE Controller

ESP32-based gesture glove with flex sensors and IMU, controlling LED strips and PC applications via Bluetooth Low Energy.

```
qd023-glove/
├── firmware/
│   └── qd023_ble_pc/          # ESP32 Arduino firmware (fw 3.2)
│       ├── qd023_ble_pc.ino   # Main sketch
│       └── web_portal.h       # Built-in web server + OTA handler
├── app/
│   ├── bridge/
│   │   └── glove_bridge.py    # Python BLE bridge (Windows)
│   └── web/
│       └── src/               # React web UI source
├── pc/
│   ├── hand_gui.py            # Standalone hand visualization
│   ├── pc_receiver.py         # BLE → keyboard/mouse bridge
│   └── pc_simple.py           # Diagnostic monitor
├── tools/
│   └── flash.ps1              # Compile + upload helper
├── docs/
│   ├── PINMAP.md              # Wiring diagram + pin assignments
│   └── PROTOCOL.md            # BLE message format reference
└── logs/                      # Serial captures (not committed)
```

## Hardware Required

| Component | Specification |
|---|---|
| Microcontroller | ESP32-WROOM-32D (ESP32-D0WD-V3) |
| IMU | MPU6050 (I2C: SDA=G21, SCL=G22) |
| Flex sensors | 5x 10K Ohm resistive bend sensors |
| ADC Pins | Thumb=G36, Index=G39, Middle=G34, Ring=G35, Pinky=G32 |
| Power | USB 5V/500mA (or external 3.3V regulator) |
| Bluetooth | BLE 4.2 (built-in ESP32) |

## Features

- **5-finger bend detection** with automatic calibration
- **Wrist twist** tracking via MPU6050 gyro integration
- **Gesture recognition**: OPEN_PALM, FIST, PEACE, INDEX_UP, THUMBS_UP, PINKY_UP
- **LED strip control**: Full LEDDMX-03 protocol support (211 effects)
- **Web portal**: On-device WiFi portal for configuration and OTA updates
- **PC bridge**: Python application for keyboard/mouse control via BLE

---

## Table of Contents

1. [Quick Start — Flash Firmware](#1-quick-start--flash-firmware)
2. [Connect to the Glove](#2-connect-to-the-glove)
3. [Use the Web Portal](#3-use-the-web-portal)
4. [Control the LED Strip](#4-control-the-led-strip)
5. [Gesture Mapping](#5-gesture-mapping)
6. [Run the PC Bridge](#6-run-the-pc-bridge)
7. [Configure Thresholds](#7-configure-thresholds)
8. [Troubleshooting](#8-troubleshooting)

---

## 1. Quick Start — Flash Firmware

### Prerequisites

- Arduino CLI installed at `C:\Users\<you>\arduino-cli\arduino-cli.exe`
- ESP32 board package installed (version 3.3.x)
- Python 3.12+ installed
- USB cable for the ESP32

### Flash via USB (one-time setup)

```powershell
# Compile
& "C:\Users\lucky\arduino-cli\arduino-cli.exe" compile `
    --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs" `
    "C:\Users\lucky\qd023-glove\firmware\qd023_ble_pc"

# Upload — hold BOOT button, tap EN, release BOOT
& "C:\Users\lucky\arduino-cli\arduino-cli.exe" upload `
    -p COM13 `
    --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs,UploadSpeed=115200" `
    "C:\Users\lucky\qd023-glove\firmware\qd023_ble_pc"
```

Or use the helper script:

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\lucky\qd023-glove\tools\flash.ps1"
```

> **Note:** If you see "Wrong boot mode detected (0x13)", hold the BOOT button, tap EN, release BOOT, then retry.

### Update via OTA (over WiFi)

Once the firmware is on the device, all future updates use the built-in OTA handler:

```powershell
# Compile first
& "C:\Users\lucky\arduino-cli\arduino-cli.exe" compile `
    --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs" `
    "C:\Users\lucky\qd023-glove\firmware\qd023_ble_pc"

# Upload over WiFi
curl.exe -sS -m 120 -H "Expect:" `
    -F "bin=@C:\Users\lucky\qd023-glove\firmware\build\qd023_ble_pc.ino.bin" `
    http://192.168.1.7/api/update
```

The glove will reboot automatically after a successful flash.

---

## 2. Connect to the Glove

### Via WiFi (Web Portal)

1. The glove creates its own access point: `QD023-Glove` (password: `glove1234`)
2. Connect your PC to this WiFi network
3. Open http://192.168.4.1 in your browser

### Via Home Network

If the glove is already connected to your home WiFi:
1. Find the glove's IP address (check your router's DHCP table, or look for the IP in the serial output)
2. Open http://<glove-ip>/ in your browser

### Via USB Serial (Debug)

```powershell
& "C:\Users\lucky\arduino-cli\arduino-cli.exe" monitor -p COM13 -c baudrate=115200
```

Serial commands:
- `c` — Run calibration
- `d` — Toggle raw sensor dump
- `s` — ADC1 pin sweep (pins 32-39)
- `z` — Zero twist angle
- `h` — Print help

---

## 3. Use the Web Portal

The web portal provides live monitoring and configuration.

### Main Page (/)

- **Live bend bars** — Real-time finger flex percentage (0-100%)
- **Twist angle** — Wrist rotation in degrees
- **Gesture display** — Current recognized gesture
- **Quick controls** — LED power, color picker, brightness slider

### LED Controls Tab

Configure the LED strip:
- **Effect selector** — Choose from 211 preset effects
- **Direction** — Forward or backward animation
- **Mic mode** — Enable microphone-reactive mode (0-4)
- **Mic sensitivity** — Adjust reactivity level

### Gestures Tab

View the complete gesture map and what each gesture controls.

### Calibration

1. Click "Calibrate" on the portal
2. Hold your hand FLAT (fingers straight) for 3 seconds
3. Make a tight FIST for 3 seconds
4. Rotate wrist palm-to-back view
5. Twist wrist clockwise, then return to center

---

## 4. Control the LED Strip

The glove controls the **LEDDMX-03** wireless LED strip via BLE.

### Protocol

The LEDDMX-03 uses a custom 9-byte BLE protocol:

```
Prefix: 0x7B
Command: 0xFF
Data: varies by command
Terminator: 0xBF
```

### Commands

| Command | Bytes | Description |
|---|---|---|
| Power | `7B 04 04 01|00…BF` / `7B 04 04 00|00…BF` | ON/OFF |
| Color | `7B FF 07 R G B FF FF BF` | Set RGB color |
| Brightness | `7B FF 01 (B*32/100) B 00 FF FF BF` | Set brightness 0-100% |
| Mode | `7B FF 03 M FF FF FF FF BF` | Set effect mode (0-210) |
| Speed | `7B FF 02 S FF 00 FF FF BF` | Set animation speed (1-255) |
| Direction | `7B FF 0D D FF FF FF FF BF` | Forward(0) / Backward(1) |
| Music | `7B FF 0B M 01 FF FF FF BF` | Mic-reactive mode (0-4) |
| Music Bright | `7B FF 01 (B*32/100) B 01 FF FF BF` | Mic mode brightness |

### REST API

All LED controls are available via the web portal's REST API:

```bash
# Get current status
curl http://192.168.1.7/api/status

# Set LED mode (effect number)
curl -X POST http://192.168.1.7/api/led -d "mc=39"

# Set direction
curl -X POST http://192.168.1.7/api/led -d "d=1"

# Enable mic mode
curl -X POST http://192.168.1.7/api/led -d "mm=2"

# Get all available modes
curl http://192.168.1.7/api/modes
```

---

## 5. Gesture Mapping

### Active Gestures

| Gesture | LED Action | PC Action |
|---|---|---|
| OPEN_PALM | Power ON | None |
| FIST | Power OFF | Play/Pause |
| PEACE (2 fingers) | Next effect | Previous |
| INDEX_UP | Next color preset | Next track |
| THUMBS_UP | Brightness +25% | Volume up |
| PINKY_UP | Brightness -25% | Volume down |
| TWIST CW | Speed +10 | Scroll up |
| TWIST CCW | Speed -10 | Scroll down |

### Requirements

- A pose must be held for **3 samples (~60ms)** to register
- Transient gestures are ignored to prevent false triggers
- Gesture recognition works even when the LED strip is disconnected

---

## 6. Run the PC Bridge

The PC bridge translates BLE messages into keyboard/mouse events.

### Install Dependencies

```powershell
pip install bleak pyautogui pycaw
```

### Run the Bridge

```powershell
cd C:\Users\lucky\qd023-glove\app\bridge
python glove_bridge.py
```

The bridge:
- Connects to `QD023-Glove` via BLE
- Listens on WebSocket `ws://127.0.0.1:8787`
- Translates gestures to keyboard/mouse events
- Auto-reconnects if the link drops

### Run the Web UI

```powershell
cd C:\Users\lucky\qd023-glove\app\web
npm install
npm run dev
```

Open http://localhost:5173 to see the live hand visualization.

### Standalone Diagnostics

```powershell
# Hand GUI viewer
python pc\hand_gui.py

# Raw message monitor
python pc\pc_receiver.py --dry-run

# Simple numeric display
python pc\pc_simple.py
```

---

## 7. Configure Thresholds

Thresholds determine when a finger is considered "bent".

### Via Web Portal

1. Open the web portal
2. Navigate to the calibration/config section
3. Adjust per-finger thresholds (0-100%)
4. Save — changes persist across reboots

### Via Bridge Config

Edit `app/bridge/config.json`:

```json
{
  "bindings": {
    "FIST": "play_pause",
    "INDEX_UP": "next",
    "PEACE": "prev"
  },
  "thresholds": {
    "thumb": { "on": 56, "off": 30 },
    "index": { "on": 46, "off": 18 },
    "middle": { "on": 46, "off": 18 },
    "ring": { "on": 46, "off": 18 },
    "pinky": { "on": 46, "off": 18 }
  }
}
```

---

## 8. Troubleshooting

### Common Issues

| Problem | Solution |
|---|---|
| "Wrong boot mode detected (0x13)" | Hold BOOT, tap EN, release BOOT, retry |
| Port busy | Close Arduino IDE Serial Monitor |
| Finger not registering | Run `d` command, check raw ADC values |
| Bleach reading 0 | Normal — finger at rest position |
| Twist drifts | Run `z` to zero, redo calibration |
| LED strip not connecting | Ensure strip is powered on, within range |
| OTA fails with "Empty reply" | Power cycle strip, retry flash |

### Reading Raw Sensors

```
Serial command: d
Output example:
T:0 I:1 M:0 R:0 P:0  raw[ 237   34    0    0    0]  OPEN_PALM BACK_VIEW
```

Column meaning:
- `T/I/M/R/P` — Bent flags (0/1)
- `raw[]` — Raw ADC values for each finger
- Last field — Current gesture and rotation state

### ADC Pin Map

| Finger | Pin | ADC1 Channel |
|---|---|---|
| Thumb | G36 | ADC1_CH0 |
| Index | G39 | ADC1_CH3 |
| Middle | G34 | ADC1_CH4 |
| Ring | G35 | ADC1_CH5 |
| Pinky | G32 | ADC1_CH6 |

---

## What I Did — Reverse Engineering the Chinese LED Strip

This project began with a proprietary Chinese LED strip controller (model LEDDMX-03) that used an undocumented BLE protocol. Rather than relying on the vendor's app, I chose to reverse engineer the entire system.

### The Reverse Engineering Process

Using the **Reverse Engineer Anything (REA)** methodology, I:

1. **Acquired the companion app APK** from the Play Store and decompiled it using `jadx`
2. **Extracted the BLE service UUIDs** from `NetConnectBle.java` — discovered the proprietary Nordic UART Service implementation
3. **Identified the 9-byte frame protocol** by analyzing packet captures and the app's byte construction logic
4. **Mapped all 211 lighting effects** from `arrays.xml` resources to their numeric codes
5. **Reconstructed the full command set**: power, color, brightness, mode, speed, direction, music-reactive mode, and mic brightness
6. **Implemented a complete open-source replacement** firmware that speaks the same protocol natively over BLE
7. **Added gesture control** so the glove can drive the strip without any phone app

### Why This Matters

The original app was a black box — no documentation, no API, no way to automate. By reverse engineering the protocol, I've created a system that:

- Works without the vendor app
- Can be controlled via gesture, web portal, or REST API
- Is fully documented and modifiable
- Can be extended with new effects or protocols

### Future Applications

This reverse-engineered protocol foundation enables:
- **Diwali light installations** — synchronized multi-zone lighting controlled by gestures
- **Home automation** — LED strips as part of a smart home ecosystem
- **Performance art** — real-time gesture-controlled lighting shows
- **Accessibility** — hands-free control of ambient lighting

The techniques learned here (APK decompilation, BLE protocol reconstruction, resource extraction) are reusable for future proprietary device integrations.

---

## License

MIT — use, modify, and distribute freely.

## Credits

- ESP32 Arduino core: https://github.com/espressif/arduino-esp32
- bleak (BLE library): https://github.com/hbldh/bleak
- LEDDMX-03 protocol: Reverse engineered from companion APK
