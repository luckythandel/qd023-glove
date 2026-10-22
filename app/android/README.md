# QD023 Glove — Android Controller

Companion Android app that controls the phone the same way the Windows bridge
controls a PC. Receives the glove's BLE NUS stream, runs the same gesture engine,
and dispatches actions via Android's AudioManager / media keys / brightness.

## Build
1. Open `app/android/` in Android Studio (Giraffe or newer).
2. Sync Gradle (minSdk 26, targetSdk 34).
3. Build & run on a device with BLE.

## Permissions
On first run grant Bluetooth, Location (≤Android 11), Notifications, and
"Modify system settings" (for brightness). For mouse/swipe gestures, enable the
QD023 accessibility service in Settings → Accessibility.

## Configuration
Config is stored at `filesDir/config.json` with the same schema as
`app/bridge/config.json` (bindings, thresholds, custom). Import/export via the
Settings screen keeps configs portable between Windows and Android.

## Status
- M1 scaffold: BLE connect, NUS parse, live finger display, foreground service.
- M2 gesture engine ported (golden tests pending).
- M3 action dispatch (volume, media, brightness) wired.
- M4 customization UI: TODO.
- M5 mouse/accessibility: service present, wiring TODO.
- M6 connection arbitration: basic reconnect; arbitration TODO.
