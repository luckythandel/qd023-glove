# Protocol

Two independent channels: **BLE** (glove → PC app) and **USB serial** (debug).

## BLE — Nordic UART Service

| Item | Value |
|---|---|
| Device name | `QD023-Glove` |
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (glove → PC) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` — `PROPERTY_NOTIFY` |
| RX (PC → glove) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` — `PROPERTY_WRITE` / `WRITE_NR` |

Every message is newline-terminated (`\n`).

### Glove → PC

| Message | When | Example |
|---|---|---|
| `READY` | after setup + first calibration | `READY` |
| `GESTURE:<NAME>` | gesture changed and held 3 samples (~450 ms) | `GESTURE:FIST` |
| `FINGERS:p0,p1,p2,p3,p4` | every 600 ms | `FINGERS:0,82,0,95,0` |
| `FINGER:<name>:BENT\|EXT` | single finger crossing | `FINGER:Index:BENT` |
| `ROT:<label>` | rotation changed and held 4 samples (~600 ms) | `ROT:PALM_VIEW` |

`FINGERS` values are **0–100 %** of that finger's travel from OPEN to FLEXED
(not raw ADC).

Gesture names:

```
OPEN_PALM  FIST  INDEX_UP  PEACE  THUMBS_UP  PINKY_UP
ONE_FINGER TWO_BENT THREE_BENT FOUR_BENT PARTIAL NEED_CALIBRATION
```

Rotation labels:

```
PALM_VIEW  BACK_VIEW
TILT_LEFT  TILT_RIGHT  TILT_FWD  TILT_BACK  FLAT
```

### PC → glove

| Message | Effect |
|---|---|
| `CAL` | run a full recalibration |

## USB serial (115200, COM13)

### Commands you type

| Key | Effect |
|---|---|
| `c` | recalibrate now |
| `d` | toggle the 5-finger raw dump (incl. spare `G33`) |
| `s` | toggle full **ADC1 sweep** `32,33,34,35,36,37,38,39` — use to find which pin a finger drives |
| `h` | print help |

### Output lines

```
=== QD023 BLE-PC Controller v2.0 ===
pins: T=33 I=39 M=34 R=35 P=32   (ADC1, 12-bit, 11dB)
MPU6050 OK
BLE advertising as QD023-Glove (NUS)

=== CALIBRATION v2.0 (per finger, adaptive) ===
Step 0: hand FLAT and relaxed, fingers STRAIGHT, in 3s...
OPEN baseline: T=608 I=40 M=0 R=0 P=0
Step 1/5: bend ONLY Thumb to a COMFORTABLE position, hold it, in 3s...
   Thumb =  603
   ...
 Thumb: open=608 bent=355 travel=253 thr=527 db=30 (bent=LOW) OK

T:0 I:1 M:0 R:0 P:0  raw[ 355  720  365  417  435]  ONE_FINGER PALM_VIEW
>>> FINGER:Index:BENT [BLE]
>>> ONE_FINGER +PALM_VIEW [BLE]
>>> ROT:PALM_VIEW [BLE]

RAWS T= 355 I=  40 M=   0 R=   0 P=   0 | G36= 608 (thr T=527 I=289 M=173 R=316 P=265)
```

Suffix `[BLE]` means the message was notified to a connected PC; `[local]` means
no BLE client (serial only).
