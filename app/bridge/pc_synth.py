"""Standalone PC synthesizer for QD023 music mode.

Runs as a child process of the bridge. Note events arrive as JSON lines on
stdin:

    {"m": 5}      finger bitmask (bit 0 = thumb .. bit 4 = pinky)
    {"b": -18.5}  wrist twist in degrees (-30..30 bends +/-2 semitones,
                  past +/-45 shifts an octave)
    {"q": 1}      quit

Keeping PortAudio in a separate process is deliberate: importing
sounddevice calls Pa_Initialize(), which puts the calling thread into a
single-threaded COM apartment and breaks bleak's WinRT callbacks.
"""
from __future__ import annotations

import json
import math
import sys
import threading

import numpy as np

try:
    import sounddevice as sd
except Exception as exc:  # pragma: no cover
    print(f"pc_synth: sounddevice unavailable: {exc}", file=sys.stderr)
    raise SystemExit(2)

SR = 48000
VOICES = 5
BASE_HZ = (523.25, 587.33, 659.25, 783.99, 880.00)
NAMES = ("C", "D", "E", "G", "A")
BLOCK = 480  # 10 ms

_lock = threading.Lock()
_target = [0.0] * VOICES
_gate = [0.0] * VOICES
_phase = [0.0] * VOICES
_bend = 0.0
_octave = 0


def _callback(outdata, frames, _time_info, _status) -> None:
    with _lock:
        targets = list(_target)
        gates = list(_gate)
        phases = list(_phase)
        bend = _bend
        octv = _octave

    n = np.arange(1, frames + 1, dtype=np.float64)
    mix = np.zeros(frames, dtype=np.float64)

    for v in range(VOICES):
        goal = targets[v]
        g = gates[v]
        rate = 0.010 if goal > g else 0.0006
        env = goal + (g - goal) * np.exp(-rate * n)
        gates[v] = float(env[-1])

        freq = BASE_HZ[v] * (2.0 ** (bend / 12.0)) * (2.0 ** octv)
        inc = 2.0 * math.pi * freq / SR
        ph = phases[v] + inc * n
        phases[v] = float(ph[-1] % (2.0 * math.pi))
        mix += env * np.sin(ph)

    with _lock:
        _gate[:] = gates
        _phase[:] = phases

    outdata[:, 0] = np.clip(mix * 0.22, -1.0, 1.0).astype(np.float32)


def apply(msg) -> bool:
    global _bend, _octave
    with _lock:
        if "m" in msg:
            mask = int(msg["m"])
            for i in range(VOICES):
                _target[i] = 1.0 if mask & (1 << i) else 0.0
        if "b" in msg:
            deg = float(msg["b"])
            _bend = max(-1.0, min(1.0, deg / 30.0))
            _octave = 1 if deg > 45 else (-1 if deg < -45 else 0)
    return bool(msg.get("q"))


def main() -> int:
    try:
        stream = sd.OutputStream(
            samplerate=SR,
            blocksize=BLOCK,
            channels=1,
            dtype="float32",
            callback=_callback,
        )
        stream.start()
    except Exception as exc:  # pragma: no cover
        print(f"pc_synth: cannot open output: {exc}", file=sys.stderr)
        return 3

    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
            except json.JSONDecodeError:
                continue
            if apply(msg):
                break
    finally:
        try:
            stream.stop()
            stream.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
