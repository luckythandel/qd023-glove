# Pin map & sensor behaviour

## Wiring (verified against live readings on this glove)

| Finger | GPIO | ADC1 channel | Rest (straight) | Flexed | Polarity |
|---|---|---|---|---|---|
| Thumb  | `33` | CH5 | ~608 | ~355 (down to ~22 at full bend) | **bent = LOW** |
| Index  | `39` | CH3 | ~0–40 | ~750 | bent = HIGH |
| Middle | `34` | CH6 | ~0 | ~530–550 | bent = HIGH |
| Ring   | `35` | CH7 | ~0 | ~950–1040 | bent = HIGH |
| Pinky  | `32` | CH4 | ~0 | ~830 | bent = HIGH |

All five are **ADC1**, which keeps working while Bluetooth is active.
(ADC2 pins would be unusable with BLE.)

Configured in firmware:

```cpp
analogReadResolution(12);
analogSetAttenuation(ADC_11db);
const int FINGER_PINS[5] = {33, 39, 34, 35, 32};
```

## The G36 question

The vendor tutorial (`docs/reference/ACB_Gloves/ACB_Gloves.h`) declares:

```cpp
const int potentiometerPin1 = 36;   // vendor's "thumb"
const int potentiometerPin2 = 39;
const int potentiometerPin3 = 34;
const int potentiometerPin4 = 35;
const int potentiometerPin5 = 32;
```

But on **this** glove the thumb sensor responds on **G33**: during calibration
`G33` fell 608 → 12 while the thumb was bent, and `G33` is otherwise rock-steady.

So the firmware uses `G33` for the thumb. To settle it for sure:

1. Start the serial monitor
2. Send `d` (raw dump on)
3. Bend **only** the thumb

```
RAWS T= 355 I=  40 M=   0 R=   0 P=   0 | G36= 608 (thr ...)
```

- `T` (G33) moves → current map is correct.
- `G36` moves → change `FINGER_PINS[0]` to `36` and reflash.

## Detection model

The tutorial uses a single fixed rule: `potValue > 200` means bent. That works
because those channels rest near 0 and rise well past 500.

This firmware keeps that *shape* but makes it adaptive per finger:

```
threshold = open + 0.32 * (bent - open)      // near the OPEN end, like >200
deadband  = clamp(0.12 * travel, 15, 60)      // hysteresis either side
polarity  = auto-detected (bent < open ? inverted : normal)
```

Why near the OPEN end rather than the midpoint: with `thr = (open+bent)/2`, Middle
landed at `490 + 48` deadband = `538` while its actual flexed value is `~540`, so it
flickered. At 32 % of travel the same finger triggers around `173`, which is far
inside the flexed range.

### Guard rails

| Condition | Result |
|---|---|
| window range < 40 or travel < 60 | `NO_TRAVEL` → keep previous values |
| travel < 120 | `WEAK` → finger marked inactive |
| all fingers inactive (< 3 active) | gesture reports `NEED_CALIBRATION` |

## Rotation

Two accel templates are learned during calibration: `PALM_VIEW` and `BACK_VIEW`.
Classification is the larger dot product of the live accel vector against each.

If the two templates are too similar (`dot > 60`, where a clean split should be
`< 40`), the firmware **discards them** and falls back to tilt-only buckets
(`TILT_LEFT / TILT_RIGHT / TILT_FWD / TILT_BACK / FLAT`). That check exists because
a run where the glove never moved produced `dot = 100.9` and meaningless templates.

Tilt fallback thresholds: ±35° on pitch and roll.

## LED

| State | Blink |
|---|---|
| No BLE client | 350 ms toggle |
| BLE connected | 900 ms toggle |
| Gesture fired | 60 ms flash |
