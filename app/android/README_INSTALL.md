# QD023 Glove — Android Install Guide

## 1. Install

```powershell
adb install -r app\android\app\build\outputs\apk\debug\app-debug.apk
```

Or copy the APK to the phone and open it (allow "install unknown apps").

## 2. First run

1. Open **QD023 Glove**.
2. Grant **Nearby devices** (Bluetooth scan/connect) and **Notifications**.
3. Optional but recommended:
   - **Accessibility service** — needed for Back/Home/Recents actions.
   - **Write system settings** — needed for brightness actions
     (`adb shell appops set com.qd023.glove android:write_settings allow`).
4. Press **START**. The service runs in the foreground and scans for the glove.

## 3. Modes

Press **K1** on the glove to cycle modes. The app only connects to the modes it
owns, so the phone and the PC never fight over the link.

| Mode | Value | Advertised as | Notes |
|---|---|---|---|
| Windows | 0 | `QD023-Glove-W` | PC bridge owns the link |
| LED strip | 1 | `QD023-Glove-L` | glove drives the strip directly |
| Android | 2 | `QD023-Glove-A` | this app — gestures become phone actions |
| Music | 3 | `QD023-Glove-M` | this app plays notes, or the PC with `--music` |

The portal (`http://<glove-ip>/`) can switch modes too (PC / LED / Phone / Music
buttons), and `/api/mode?m=<0-3>` does the same over HTTP.

## 4. Customize bindings

Home → **CUSTOMIZE BINDINGS** → tap a gesture → pick an action → **SAVE**.
Saved to `config.json` inside the app and pushed to the running service.

Available actions: play/pause, next, previous, stop, volume ±, mute,
brighter, dimmer, back, home, recents, none.

## 5. Music Hand mode

Switch the glove to **MUSIC** (K1, or the portal's *Music* button), then:

- Thumb → C5, index → D5, middle → E5, ring → G5, pinky → A5
- Twist the wrist to bend ±2 semitones; past ±45° jumps an octave
- The ACTION row shows the notes currently held

To play on the **PC** instead:

```powershell
cd app\bridge
pip install numpy sounddevice
python glove_bridge.py --music
```

Only one output can hold the link: stop the phone service if you want PC audio,
or stop the bridge if you want phone audio.

## 6. Find synthesizer outputs

```powershell
cd app\bridge
python scan_synth.py
```

Lists audio outputs on this PC, Android devices on adb, nearby BLE devices
(with the glove's advertised mode) and LAN hosts with synth-ish ports.

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| MODE stays WINDOWS | Reconnect (Restart) — mode is read with `GETMODE` after notifications are enabled |
| App stuck on SEARCHING | Check the glove advertises `-A`/`-M`; the scan re-arms every 8 s |
| No sound in MUSIC mode | Phone volume / Do Not Disturb; the synth uses the media stream |
| PC synth silent | `python glove_bridge.py --list-audio` to confirm an output exists |
| Actions do nothing | Grant Accessibility and the settings appop |

## 8. Uninstall

```powershell
adb uninstall com.qd023.glove
```
