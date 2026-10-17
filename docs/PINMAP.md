# Pin map & sensor behaviour

## Wiring (verified against live readings on this glove)

| Finger | GPIO | ADC1 channel | Rest (straight) | Flexed | Polarity |
|---|---|---|---|---|---|
| Thumb  | `36` | CH0 | ~0 | up to ~816 | bent = HIGH |
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
const int FINGER_PINS[5] = {36, 39, 34, 35, 32};
```

## Why the thumb is on G36 (settled)

The vendor tutorial (`docs/reference/ACB_Gloves/ACB_Gloves.h`) declares:

```cpp
const int potentiometerPin1 = 36;   // thumb
const int potentiometerPin2 = 39;   // index
const int potentiometerPin3 = 34;   // middle
const int potentiometerPin4 = 35;   // ring
const int potentiometerPin5 = 32;   // pinky
```

An early version of this firmware used `G33` for the thumb and `G36` was left as an
unused "EXTRA_PIN". That was wrong. Measured evidence:

| Test | `G33` | `G36` |
|---|---|---|
| Idle (glove worn, fingers straight) | 604 | 0 |
| Thumb bend only, 150 s | no movement (span 12) | three clear windows, peaks 631 / 816 / 494 |
| Full finger scan | one 284-count blip (crosstalk) | repeated multi-hundred swings |

`G33` is simply **unconnected** on this glove: it floats at ~604 and only twitches
with electrical noise. That is what produced the misleading `608 → 12` "calibration"
reading earlier — a floating pin snapping to ground, not a real bend.

So the thumb is on **G36**, resting at 0 and rising when bent — the same polarity as
the other four. The firmware no longer hardcodes any polarity; it measures it.

To re-check on the bench: send `d` in the serial monitor. The dump now shows the
spare pin explicitly:

```
RAWS T= 612 I=  40 M=   0 R=   0 P=   0 | G33(spare)= 604 (thr T=192 I=289 M=173 R=316 P=265)
```

`T` should track the thumb; `G33` should sit near 604 and never follow it.

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
