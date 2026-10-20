# Pin map & sensor behaviour

> **The pin map is FIXED** (firmware v2.1). It was measured on this unit and matches
> the vendor tutorial: Thumb=G36, Index=G39, Middle=G34, Ring=G35, Pinky=G32.
> v2.0 tried to *learn* pins at calibration by scanning G32–G39 per finger, but the
> unconnected pin G33 (floats ~604, twitches with noise) could win the "biggest
> mover" contest and bind a finger to noise — that is why some fingers never moved
> in the GUI. v2.1 trusts the measured map and calibrates only the *thresholds*
> (two poses: flat hand + fist, median of 40 samples each).

## Wiring (fixed in firmware, verified on this glove)

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

Firmware v2.1 keeps that *shape* but makes it adaptive per finger, from a
**two-pose calibration** (hand FLAT then full FIST, each a median of 40 samples):

```
threshold = open + bendFraction * (fist - open)  // near the OPEN end, like >200
deadband  = clamp(0.12 * travel, 15, 60)         // hysteresis either side
polarity  = auto-detected (fist < open ? inverted : normal)
```

`bendFraction` defaults to `0.32` and `minTravel` to `80`; both are read from NVS
(namespace `glove`) at boot and can be changed on the on-board portal. The raw
`thr[]` values stay calibration-derived — only the *percent* on/off bands are shared
with the PC bridge.

Why near the OPEN end rather than the midpoint: with `thr = (open+fist)/2`, Middle
landed at `490 + 48` deadband = `538` while its actual flexed value is `~540`, so it
flickered. At 32 % of travel the same finger triggers around `173`, which is far
inside the flexed range.

`bentPct` maps the live ADC to 0–100 % of `fist − open` for the GUI, clamped to
range. The median sampling rejects single-sample ADC spikes that used to poison
thresholds (one bad capture gave thumb `thr=794` while the real max is ~816, so the
thumb never registered as bent).

### Guard rails

| Condition | Result |
|---|---|
| travel < `minTravel` (default 80) | finger marked `INACTIVE` (thresholds ignored) |
| < 3 fingers active | gesture reports `NEED_CALIBRATION` |

## Rotation

Two accel templates are learned during calibration: `PALM_VIEW` and `BACK_VIEW`.
Classification is the larger dot product of the live accel vector against each.

If the two templates are too similar (`dot > 60`, where a clean split should be
`< 40`), the firmware **discards them** and falls back to tilt-only buckets
(`TILT_LEFT / TILT_RIGHT / TILT_FWD / TILT_BACK / FLAT`). That check exists because
a run where the glove never moved produced `dot = 100.9` and meaningless templates.

Tilt fallback thresholds: ±35° on pitch and roll.

## Wrist twist (clockwise / anticlockwise + degrees)

`PALM_VIEW` / `BACK_VIEW` is a two-state guess from the accelerometer — it says
which way the palm faces, but not how far you turned or in which direction.

The gyro can: rotate the wrist (pronation / supination) and the gyro reports an
angular **rate**. Integrate it over time and you get a signed angle.

Calibration learns two things automatically:

1. **Zero-rate bias** — 1.5 s of holding still, averaged per axis. Without this the
   integration drifts even when you're motionless.
2. **Which axis and which sign** — you rotate the wrist **clockwise** for 3 s; the
   axis with the largest mean rate is the wrist axis, and the sign of that mean
   becomes "clockwise = positive".

Runtime:

```
rate  = (gyro[axis] - bias[axis]) * sign * 57.2958   // deg/s
rate  = 0 if |rate| < 3 deg/s                        // deadband, kills drift
angle += rate * dt                                   // dt clamped to 0.5 s
```

Emitted as `TWIST:<deg>`, clamped to ±720°, and zeroed by the `z` command.

**Drift note:** pure integration accumulates error over minutes. The deadband and
bias cancelling handle the common case (reporting a twist gesture), and `z` gives a
manual re-zero. A drift-free absolute angle would need the accelerometer gravity
vector to anchor the gyro — that is the natural next step if you want it to stay
accurate across long sessions.

## LED

| State | Blink |
|---|---|
| No BLE client | `ledFastMs` toggle (default 350 ms) |
| BLE connected | `ledSlowMs` toggle (default 900 ms) |
| Gesture fired | 60 ms flash |

Both intervals are configurable on the on-board portal (namespace `glove`).

## Config store (NVS `glove`)

| Key | Default | Effect |
|---|---|---|
| `name` | `QD023-Glove` | BLE name **and** AP SSID |
| `sem` | 600 | `FINGERS:` stream period |
| `lf` / `ls` | 350 / 900 | LED toggle intervals |
| `bf` | 0.32 | threshold position between open and fist |
| `mt` | 80 | below this a finger is `INACTIVE` |
| `on0`..`on4` / `off0`..`off4` | `56,46,46,46,46` / `30,18,18,18,18` | percent bands shared with the bridge |
| `tp` / `tr` | 58 / 60 | twist hold-freeze gates, deg/s |
| `ta` / `ts` | -1 / 0.0 | `-1`/`0.0` = auto from calibration |

`twAxis`/`twSign` default to auto deliberately: a stored axis would override
calibration and pin the twist to a stale axis after the glove is reworn.
