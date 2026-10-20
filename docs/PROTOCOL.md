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
| `TWIST:<deg>` | wrist twist changed ≥1° (or every 500 ms) | `TWIST:-37` |

`TWIST` is a **signed angle in degrees** since the last zero: positive = clockwise,
negative = anticlockwise. It is integrated from the gyro about the axis chosen during
calibration, with a ±3 °/s deadband so it doesn't drift while your hand is still.

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
| `s` | toggle ADC1 sweep of the 6 broken-out pins `32,33,34,35,36,39` (G37/G38 absent on WROOM-32D) |
| `z` | zero the wrist-twist angle |
| `h` | print help |

### Output lines

```
=== QD023 BLE-PC Controller v2.1 ===
pins: T=36 I=39 M=34 R=35 P=32   (ADC1, 12-bit, 11dB)
MPU6050 OK
BLE advertising as QD023-Glove (NUS)

=== CALIBRATION v2.1 (two pose: FLAT + FIST) ===
Step 1/2: hand FLAT, fingers STRAIGHT and relaxed, in 3s...
OPEN : Thumb=0 Index=40 Middle=0 Ring=0 Pinky=0
Step 2/2: make a FIST (curl ALL five fingers), hold it, in 3s...
FIST : Thumb=816 Index=750 Middle=540 Ring=980 Pinky=830
 Thumb: open=0 fist=816 travel=816 thr=261 db=60 (bent=HIGH) OK
 Index: open=40 fist=750 travel=710 thr=267 db=60 (bent=HIGH) OK
 ...
MAPPING: Thumb=G36 Index=G39 Middle=G34 Ring=G35 Pinky=G32  active=5/5

T:0 I:1 M:0 R:0 P:0  raw[ 355  720  365  417  435]  ONE_FINGER PALM_VIEW
>>> FINGER:Index:BENT [BLE]
>>> ONE_FINGER +PALM_VIEW [BLE]
>>> ROT:PALM_VIEW [BLE]

RAWS T= 355 I=  40 M=   0 R=   0 P=   0 | G33(spare)= 604 (thr T=261 I=267 M=173 R=316 P=265)
```

Suffix `[BLE]` means the message was notified to a connected PC; `[local]` means
no BLE client (serial only).
