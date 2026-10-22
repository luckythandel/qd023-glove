# QD023 Glove — Android App Release Notes

## v1.1 — Music Hand mode

### Artifacts

| Item | Path | Size | Version |
|---|---|---|---|
| Android APK | `app/android/app/build/outputs/apk/debug/app-debug.apk` | ~9.5 MB | 1.1 (debug) |
| Firmware | `firmware/build/qd023_ble_pc.ino.bin` | ~1.82 MB | 3.4 |

Minimum Android: 8.0 (API 26) · Built for: API 34 · Tested on: vivo V2515 (API 36)

### New in this build

- **Music Hand mode (4th mode, `ctl = 3`)**
  - Five fingers gate five notes: C5 D5 E5 G5 A5 (C-major pentatonic)
  - Wrist twist bends ±2 semitones, past ±45° shifts an octave
  - `SynthEngine`: 5-voice AudioTrack synth, 48 kHz mono, 10 ms chunks
  - Notes streamed from the glove as `MUS:<mask>`
- **Mode query fix** — the app sends `GETMODE` once notifications are live, so
  the MODE row shows the real mode instead of always WINDOWS.
- **Reliable mode hand-off**
  - Matching now uses the manufacturer-data mode byte (company `0xFFFF`) instead
    of the device name, which can be a stale GAP cache.
  - The app claims both `-A` (control) and `-M` (music) advertisements.
- **Scan watchdog** — an idle BLE scan is re-armed every 8 s, so a mode change on
  the glove is picked up without restarting the service.
- **Restart button** — now stops and restarts the service (a second
  `startForegroundService()` never re-runs `onCreate`, so the scan stayed stale).
- **Binding fixes** — saving writes `config.json` and reloads the running
  service; OPEN_PALM defaults to NONE.

### Modes

| Mode | Value | Advertised as | Who acts |
|---|---|---|---|
| Windows | 0 | `QD023-Glove-W` | PC bridge |
| LED strip | 1 | `QD023-Glove-L` | Glove → LED strip |
| Android | 2 | `QD023-Glove-A` | This app |
| Music | 3 | `QD023-Glove-M` | This app **or** PC bridge (`--music`) |

### Known limitations

- Only one client can hold the BLE link; the firmware disconnects the old one
  when the mode changes and re-advertises under the new tag.
- Music mode needs the glove to be connected; notes stop on disconnect.
- PC synthesis needs `numpy` and `sounddevice` in the bridge's Python.
