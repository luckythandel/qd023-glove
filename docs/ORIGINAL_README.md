# QD023 Glove BLE PC Controller

Glove (ESP32-WROOM-32D, WiFi + BT4.2/BLE) sends gestures over BLE Nordic-UART.
PC Python receives and presses keys for presentations / games.

## Yes: WiFi + Bluetooth?
- WiFi 802.11 b/g/n, Bluetooth Classic + BLE 4.2. This project uses **BLE**.
- Device: `QD023-Glove`, Service `6E400001-...`, TX notify `6E400003-...`

## Files
- `qd023_ble_pc.ino` — glove sender, auto-calibrates OPEN 3s / FIST 3s on boot
- `pc_receiver.py` — PC side (bleak + pyautogui)

## Gesture map (presentation default)
| Glove | PC key | Use |
|---|---|---|
| OPEN_PALM | F5 | Start slideshow |
| FIST | ESC | End/back |
| INDEX_UP | Right | Next slide |
| PEACE | Left | Prev slide |
| THUMBS_UP | b | Blank |
| PINKY_UP | m | Mute |
Game mode (`--game`): FIST=Space, INDEX_UP=E, PEACE=Q

## Run
1. Flash glove (hold BOOT, tap EN):
```
C:\Users\lucky\arduino-cli\arduino-cli.exe upload -p COM13 --fqbn "esp32:esp32:esp32:UploadSpeed=115200" C:\Users\lucky\qd023_ble_pc
```
2. Tap EN, do OPEN 3s / FIST 3s calibration in Serial Monitor.
3. On PC (Bluetooth ON, close Arduino monitor so COM13 free):
```
C:\Python312\python.exe C:\Users\lucky\qd023_ble_pc\pc_receiver.py --dry-run
C:\Python312\python.exe C:\Users\lucky\qd023_ble_pc\pc_receiver.py
C:\Python312\python.exe C:\Users\lucky\qd023_ble_pc\pc_receiver.py --game
```
