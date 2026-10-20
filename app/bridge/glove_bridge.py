#!/usr/bin/env python3
"""
QD023 Glove Bridge
BLE Nordic UART -> per-finger-threshold gesture engine -> media keys / mouse -> WebSocket.

Usage:
  C:\\Python312\\python.exe glove_bridge.py              # real BLE
  C:\\Python312\\python.exe glove_bridge.py --simulate   # fake stream, no hardware
  C:\\Python312\\python.exe glove_bridge.py --port 8787 --device-name QD023-Glove

Protocol: see ..\\PROTOCOL.md  (source of truth)
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging
import math
import os
import random
import secrets
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set

# ---------------------------------------------------------------------------
# Constants (PROTOCOL.md)
# ---------------------------------------------------------------------------
NUS_SERVICE = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_TX_CHAR = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  # notify (glove -> PC)
NUS_RX_CHAR = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  # write   (PC -> glove)

TWIST_STEP_DEG = 10    # fallback key-step (deg) when pycaw is unavailable
TWIST_FULL_DEG = 180.0 # half turn = full volume span (absolute MID_RING mode)
TWIST_INPUT_SIGN = -1.0 # raw-sign normalization: this unit reports negative deg for physical CW
ROT_TRIGGER_DEG = 45.0 # wrist twist needed to satisfy a custom rot gate; wide
                       # enough that idle hand jitter can never trip it
VOLUME_SET_MIN_MS = 60   # min interval between absolute volume writes
VOLUME_UI_STEP_PCT = 10  # flash a volume toast every 10% of movement
BRIGHTNESS_REFRESH_S = 5.0  # periodic brightness re-read while the pump runs
# hold-freeze motion FSM: measured gyro drift on this unit is ~31-54 deg/s
# while the wrist rests (logs: angle slides to the +/-720 clamp unattended),
# so the stillness threshold sits above that band and the resume threshold
# above it in turn -- both edges re-anchor delta to 0, so the gap is safe
TWIST_PAUSE_DPS = 58.0    # |rate| at/below this counts as a still wrist
TWIST_PAUSE_SAMPLES = 2   # consecutive still samples before volume freezes
TWIST_RESUME_DPS = 60.0   # |rate| at/above this counts as a deliberate twist
TWIST_RESUME_SAMPLES = 2  # consecutive twist samples before volume unfreezes
TWIST_RATE_DT_MAX_S = 1.5 # packet gap above this = stale angle, treat as still

# RING_ONLY sweep brightness: firmware sends SWEEP:deg (integrated yaw about
# gravity). A hand sweep is a slower, broader motion than a wrist twist, so
# the still/motion thresholds sit well below the twist band above.
SWEEP_INPUT_SIGN = -1.0    # right-hand lateral sweep reports negative CW yaw
SWEEP_FULL_DEG = 90.0      # +/-90 deg of sweep = full 0-100 brightness span
SWEEP_PAUSE_DPS = 12.0     # |rate| at/below this counts as a parked hand
SWEEP_RESUME_DPS = 20.0    # |rate| at/above this counts as a deliberate sweep

FINGER_NAME_TO_IDX = {"thumb": 0, "index": 1, "middle": 2, "ring": 3, "pinky": 4}
FINGER_ORDER = ["thumb", "index", "middle", "ring", "pinky"]
DEFAULT_THRESHOLDS: Dict[str, Dict[str, int]] = {
    "thumb":  {"on": 56, "off": 30},
    "index":  {"on": 46, "off": 18},
    "middle": {"on": 46, "off": 18},
    "ring":   {"on": 46, "off": 18},
    "pinky":  {"on": 46, "off": 18},
}

GESTURE_IDS = [
    "FIST", "INDEX_UP", "PEACE", "THUMBS_UP", "THUMB_BENT",
    "MIDDLE_UP", "PINKY_UP", "OPEN_PALM", "MID_RING",
    "RING_ONLY",
]
ACTION_IDS = [
    "play_pause", "next", "prev", "volume_up",
    "volume_down", "mute", "stop",
    "mouse_left", "mouse_right", "mouse_up", "mouse_down",
    "mouse_click", "mouse_rclick",
    "brightness_up", "brightness_down",
    "none",
]
DEFAULT_BINDINGS: Dict[str, str] = {
    "FIST": "play_pause",
    "INDEX_UP": "next",
    "PEACE": "prev",
    "THUMBS_UP": "none",
    "THUMB_BENT": "none",
    "MIDDLE_UP": "volume_down",
    "PINKY_UP": "mute",
    "OPEN_PALM": "none",
    "MID_RING": "none",  # twist-pose: drives absolute volume directly
    "RING_ONLY": "none",  # sweep-pose: drives absolute brightness directly
}
# LED strip control mode: pose actions reroute to BLE commands for the glove's
# LED engine (the glove parses LEDP/LEDM/LEDB/LEDC lines). Unmapped actions
# (mouse moves, stop) have no strip equivalent and are ignored.
LED_ACTION_MAP: Dict[str, str] = {
    "play_pause": "LEDP:T",
    "next": "LEDM:+1",
    "prev": "LEDM:-1",
    "volume_up": "LEDB:+10",
    "volume_down": "LEDB:-10",
    "brightness_up": "LEDB:+10",
    "brightness_down": "LEDB:-10",
    "mute": "LEDC:cycle",
}
# pyautogui key names
ACTION_TO_KEY = {
    "play_pause": "playpause",
    "next": "nexttrack",
    "prev": "prevtrack",
    "volume_up": "volumeup",
    "volume_down": "volumedown",
    "mute": "volumemute",
    "stop": "playpause",  # best-effort: toggle to pause
}

POSE_STABLE_MS = 150  # pose must hold this long before it fires
COOLDOWN_MS = 400
VOLUME_REPEAT_MS = 250
VOLUME_ACTIONS = {"volume_up", "volume_down"}
BRIGHTNESS_ACTIONS = {"brightness_up", "brightness_down"}
MOUSE_NUDGE_PX = 60    # cursor step per repeat tick (PROTOCOL.md mouse table)
MOUSE_REPEAT_MS = 80   # held-pose re-fire cadence for cursor nudges
MOUSE_NUDGE_ACTIONS = {"mouse_left", "mouse_right", "mouse_up", "mouse_down"}
NUDGE_DELTAS = {
    "mouse_left": (-MOUSE_NUDGE_PX, 0),
    "mouse_right": (MOUSE_NUDGE_PX, 0),
    "mouse_up": (0, -MOUSE_NUDGE_PX),
    "mouse_down": (0, MOUSE_NUDGE_PX),
}
MAX_CUSTOM = 12
CUSTOM_ID_LEN = 32
CUSTOM_NAME_LEN = 24

CONFIG_PATH = Path(__file__).resolve().parent / "config.json"

log = logging.getLogger("bridge")

# ---------------------------------------------------------------------------
# Config persistence
# ---------------------------------------------------------------------------
def _as_int(v: Any) -> Optional[int]:
    try:
        return int(round(float(v)))
    except (TypeError, ValueError):
        return None


def _clean_bindings(raw: Any, current: Dict[str, str]) -> Dict[str, str]:
    # subset semantics: gestures absent from `raw` keep their current action
    incoming = raw if isinstance(raw, dict) else {}
    clean: Dict[str, str] = {}
    for g in GESTURE_IDS:
        a = incoming.get(g, current.get(g, DEFAULT_BINDINGS.get(g, "none")))
        clean[g] = a if a in ACTION_IDS else "none"
    return clean


def _clean_thresholds(
    raw: Any, current: Dict[str, Dict[str, int]]
) -> Dict[str, Dict[str, int]]:
    # subset semantics: fingers absent from `raw` keep their current band
    incoming = raw if isinstance(raw, dict) else {}
    out: Dict[str, Dict[str, int]] = {}
    for name in FINGER_ORDER:
        base = current.get(name, DEFAULT_THRESHOLDS[name])
        on = int(base["on"])
        off = int(base["off"])
        entry = incoming.get(name)
        if isinstance(entry, dict) and "on" in entry:
            parsed = _as_int(entry.get("on"))
            if parsed is not None:
                on = max(15, min(95, parsed))
        if isinstance(entry, dict) and "off" in entry:
            parsed = _as_int(entry.get("off"))
            if parsed is not None:
                off = parsed
        # band must stay open below `on`, else hysteresis deadlocks
        out[name] = {"on": on, "off": max(5, min(off, on - 5))}
    return out


def _clean_custom(raw: Any) -> List[Dict[str, Any]]:
    if not isinstance(raw, list):
        return []
    by_id: Dict[str, Dict[str, Any]] = {}
    for item in raw:
        if not isinstance(item, dict):
            continue
        pat_raw = item.get("pattern")
        if not isinstance(pat_raw, list) or len(pat_raw) != 5:
            continue
        pat: List[str] = []
        valid = True
        for ch in pat_raw:
            s = str(ch).strip().upper()
            if s in ("B", "E", "X"):
                pat.append(s)
            else:
                valid = False
                break
        if not valid:
            continue
        cid = item.get("id")
        if not isinstance(cid, str) or not cid.strip() or len(cid) > CUSTOM_ID_LEN:
            cid = secrets.token_hex(4)
        name = item.get("name")
        if not isinstance(name, str) or not name.strip():
            name = "Custom"
        action = item.get("action")
        rot = item.get("rot")
        by_id[cid] = {
            "id": cid,
            "name": name.strip()[:CUSTOM_NAME_LEN],
            "pattern": pat,
            "action": action if action in ACTION_IDS else "none",
            "rot": rot if rot in ("cw", "ccw") else "none",
        }
        # duplicate id: later entry overwrites (dict keeps first-seen order)
    return list(by_id.values())[:MAX_CUSTOM]


def load_config() -> Dict[str, Any]:
    cfg: Dict[str, Any] = {
        "bindings": dict(DEFAULT_BINDINGS),
        "thresholds": {k: dict(v) for k, v in DEFAULT_THRESHOLDS.items()},
        "custom": [],
    }
    try:
        if CONFIG_PATH.exists():
            data = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                cfg["bindings"] = _clean_bindings(data.get("bindings"), cfg["bindings"])
                cfg["thresholds"] = _clean_thresholds(
                    data.get("thresholds"), cfg["thresholds"]
                )
                if isinstance(data.get("custom"), list):
                    cfg["custom"] = _clean_custom(data.get("custom"))
    except Exception as e:
        log.warning("config load failed: %s", e)
    return cfg


def save_config(cfg: Dict[str, Any]) -> None:
    try:
        CONFIG_PATH.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
    except Exception as e:
        log.warning("config save failed: %s", e)

# ---------------------------------------------------------------------------
# Gesture engine (PROTOCOL.md bend model)
# ---------------------------------------------------------------------------
def classify_pose(bent: List[bool]) -> Optional[str]:
    """bent = [t, i, m, r, p]. Rules are pairwise disjoint; FIST/OPEN_PALM first."""
    t, i, m, r, p = bent
    if all(bent):
        return "FIST"
    if not any(bent):
        return "OPEN_PALM"
    if t and not i and not m and not r and not p:
        return "THUMB_BENT"
    if not t and i and m and r and p:
        return "THUMBS_UP"
    if not i and m and r and p:
        return "INDEX_UP"
    if not i and not m and r and p:
        return "PEACE"
    if not m and i and r and p:
        return "MIDDLE_UP"
    if i and m and r and not p:
        return "PINKY_UP"
    if not i and m and r and not p:
        return "MID_RING"
    if not i and not m and r and not p:
        # ring-only bend: thumb ignored; disjoint from PEACE (pinky differs)
        # and MID_RING (middle differs) -- drives absolute brightness
        return "RING_ONLY"
    return None


def match_custom(
    bent: List[bool], custom: List[Dict[str, Any]]
) -> Optional[Dict[str, Any]]:
    for entry in custom:
        pat = entry.get("pattern")
        if not isinstance(pat, list) or len(pat) != 5:
            continue
        hit = True
        for i, want in enumerate(pat):
            if want == "X":
                continue
            if (want == "B") != bool(bent[i]):
                hit = False
                break
        if hit:
            return entry
    return None


class GestureEngine:
    def __init__(
        self,
        bindings: Dict[str, str],
        thresholds: Optional[Dict[str, Dict[str, int]]] = None,
        custom: Optional[List[Dict[str, Any]]] = None,
    ) -> None:
        self.bindings = bindings
        self.thresholds = thresholds or {
            k: dict(v) for k, v in DEFAULT_THRESHOLDS.items()
        }
        self.custom = custom or []
        self.state: List[Optional[bool]] = [None, None, None, None, None]
        self._candidate: Optional[str] = None
        self._candidate_since: float = 0.0
        self.active_pose: Optional[str] = None  # GestureId or "custom:<id>"
        self.active_display: Optional[str] = None
        self._last_fire: Dict[str, float] = {}  # action -> monotonic ts
        # absolute twist-volume state (MID_RING)
        self._twist_baseline: Optional[float] = None
        self._twist_v0: Optional[int] = None          # volume % at pose entry
        self._twist_last_set: Optional[int] = None    # last value written
        self._twist_last_set_ts: float = 0.0
        self._twist_last_ui: Optional[int] = None     # last value flashed to UI
        # hold-freeze motion FSM: a resting wrist must never change volume
        self._twist_motion: str = "frozen"  # "frozen" (baseline follows) | "active"
        self.twist_pause_dps: float = TWIST_PAUSE_DPS   # glove may override via TWD
        self.twist_resume_dps: float = TWIST_RESUME_DPS
        self._twist_slow_n: int = 0   # consecutive samples at/below twist_pause_dps
        self._twist_fast_n: int = 0   # consecutive samples at/above TWIST_RESUME_DPS
        self._twist_prev_deg: Optional[float] = None  # prior sample for rate calc
        self._twist_prev_ts: Optional[float] = None   # prior sample's monotonic ts
        # which absolute backend the current twist pose drives:
        # "volume" (MID_RING) | "brightness" (RING_ONLY); None = no twist pose
        self._twist_backend: Optional[str] = None
        self._sweep_in_ring: bool = False   # tracks RING_ONLY entry for sweep re-anchor
        # custom-gesture rotation gate (PROTOCOL.md `rot` field)
        self._rot_deg: Optional[float] = None      # latest TWIST reading (absolute deg)
        self._rot_ref_deg: Optional[float] = None  # baseline latched at pattern first-sight
        self._rot_ref_id: Optional[str] = None     # gesture the baseline belongs to
        # LED strip control mode: when the glove reports MOD:1 the bridge
        # reroutes gestures to the strip instead of Windows media keys.
        self.led_mode = False
        self._led_sink: Optional[Callable[[str], None]] = None

    def set_bindings(self, bindings: Dict[str, str]) -> None:
        self.bindings = bindings

    def set_thresholds(self, thresholds: Dict[str, Dict[str, int]]) -> None:
        self.thresholds = thresholds

    def set_custom(self, custom: List[Dict[str, Any]]) -> None:
        self.custom = custom
        # gesture ids can change on edit: a stale baseline would gate the wrong pose
        self._rot_ref_id = None
        self._rot_ref_deg = None

    def display_name(self, key: Optional[str]) -> Optional[str]:
        if key is None:
            return None
        if key.startswith("custom:"):
            cid = key.split(":", 1)[1]
            for entry in self.custom:
                if entry.get("id") == cid:
                    return str(entry.get("name", "Custom"))
            return "Custom"
        return key

    def _action_for(self, key: Optional[str]) -> Optional[str]:
        if key is None:
            return None
        if key.startswith("custom:"):
            cid = key.split(":", 1)[1]
            for entry in self.custom:
                if entry.get("id") == cid:
                    return str(entry.get("action", "none"))
            return None
        return self.bindings.get(key, "none")

    def bent_flags(self) -> List[bool]:
        return [bool(b) for b in self.state]

    def _reconcile(self, idx: int, p: int) -> None:
        th = self.thresholds.get(
            FINGER_ORDER[idx], DEFAULT_THRESHOLDS[FINGER_ORDER[idx]]
        )
        on = int(th["on"])
        off = int(th["off"])
        cur = self.state[idx]
        if cur is None:
            # seed immediately so a mid-range first sample can never deadlock
            self.state[idx] = p >= on
            return
        if p >= on:
            self.state[idx] = True
        elif p <= off:
            self.state[idx] = False

    def on_fingers(self, pct: List[int]) -> tuple[Optional[str], Optional[str]]:
        for idx in range(min(5, len(pct))):
            self._reconcile(idx, pct[idx])
        return self._tick()

    def on_finger_edge(self, name: str, bent: bool) -> tuple[Optional[str], Optional[str]]:
        idx = FINGER_NAME_TO_IDX.get(name.strip().lower())
        if idx is None or idx == 0:
            # thumb edges fire at the firmware's ~32% travel threshold;
            # the per-finger on/off rule is enforced through the FINGERS pct
            # reconcile instead
            return (None, None)
        self.state[idx] = bent
        return self._tick()

    def _twist_reset(self) -> None:
        """Drop all twist state so the next pose entry re-anchors everything."""
        self._twist_baseline = None
        self._twist_v0 = None
        self._twist_last_set = None
        self._twist_last_set_ts = 0.0
        self._twist_last_ui = None
        self._twist_motion = "frozen"
        self._twist_slow_n = 0
        self._twist_fast_n = 0
        self._twist_prev_deg = None
        self._twist_prev_ts = None
        self._twist_backend = None
        self._sweep_in_ring = False

    def on_twist(self, deg: float) -> tuple[Optional[str], Optional[str]]:
        """MID_RING wrist rotation -> absolute volume via the shared FSM.

        Returns (media_key_action, ui_flash). RING_ONLY now belongs to
        on_sweep: the angle is still recorded for rot gates, but no
        baseline/rate bookkeeping runs on this channel while that pose holds.
        """
        # rot gates read the angle outside MID_RING too, so record before gating
        self._rot_deg = float(deg)
        if self.active_pose == "RING_ONLY":
            return (None, None)
        if self.active_pose != "MID_RING":
            self._twist_reset()
            return (None, None)
        return self._abs_fsm(deg, "volume", self.twist_pause_dps, self.twist_resume_dps)

    def on_sweep(self, deg: float) -> tuple[Optional[str], Optional[str]]:
        """RING_ONLY lateral hand sweep -> absolute brightness via the shared FSM.

        Outside RING_ONLY nothing runs and nothing is reset: the firmware's
        TWIST heartbeat still arrives and owns the reset path (the pose gate
        or the backend-switch check inside _abs_fsm clears stale state on the
        next twist packet).
        """
        if self.active_pose != "RING_ONLY":
            self._sweep_in_ring = False
            return (None, None)
        # Re-anchor on entry: brightness starts at the current level and only
        # changes on a deliberate sweep, so releasing and re-entering RING_ONLY
        # can't drift the value from a stale baseline.
        if not self._sweep_in_ring:
            self._twist_baseline = None
        self._sweep_in_ring = True
        return self._abs_fsm(deg, "brightness", SWEEP_PAUSE_DPS, SWEEP_RESUME_DPS)

    def _abs_fsm(
        self, deg: float, backend: str, pause_dps: float, resume_dps: float
    ) -> tuple[Optional[str], Optional[str]]:
        """Hold-freeze absolute FSM shared by twist-volume and sweep-brightness.

        From the angle at pose entry, +full span (TWIST_FULL_DEG for volume,
        SWEEP_FULL_DEG for brightness) drives the backend to 100% and the
        negative span to 0%, clamped. The value only changes while motion is
        actually detected; a parked wrist/hand freezes the baseline so gyro
        drift cannot move it. Volume falls back to volume_up/down key steps if
        pycaw is unavailable; brightness never falls back to key steps.
        """
        now = time.monotonic()
        rate: Optional[float] = None
        if self._twist_prev_ts is not None and self._twist_prev_deg is not None:
            dt = now - self._twist_prev_ts
            # a stalled packet stream is a held wrist, not a twist
            rate = (
                0.0
                if (dt <= 0.0 or dt > TWIST_RATE_DT_MAX_S)
                else (float(deg) - self._twist_prev_deg) / dt
            )
        self._twist_prev_deg = float(deg)
        self._twist_prev_ts = now

        if self.active_pose not in ("MID_RING", "RING_ONLY"):
            self._twist_reset()
            return (None, None)
        if self._twist_backend is not None and self._twist_backend != backend:
            # switched pose without leaving (MID_RING <-> RING_ONLY):
            # the old backend's v0/last_set must never leak into the new one
            self._twist_reset()
        self._twist_backend = backend

        if rate is not None:
            a = abs(rate)
            if a <= pause_dps:
                self._twist_slow_n += 1
                self._twist_fast_n = 0
            elif a >= resume_dps:
                self._twist_fast_n += 1
                self._twist_slow_n = 0
            else:
                # dead band between pause and resume: ambiguous rate, hold
                # state -- no write, no counter advance -- so drift in this
                # band can't leak into brightness changes
                return (None, None)

        if self._twist_baseline is None:
            # pose entry starts frozen so residual drift can't move the value
            if self.led_mode:
                # LED strip: the bridge can't read the strip's real level, so
                # 50 is a neutral re-anchor; the first deliberate motion maps
                # predictably from the entry angle
                v = 50
            elif backend == "brightness":
                v = read_brightness_pct()
                if v is None:
                    # cold cache: kick a WMI read, latch nothing, retry on the
                    # next sample -- brightness must never fall back to volume
                    refresh_brightness()
                    return (None, None)
            else:
                v = read_volume_pct()
            self._twist_baseline = float(deg)
            self._twist_v0 = v
            self._twist_last_ui = v
            self._twist_last_set = None
            self._twist_motion = "frozen"
            self._twist_slow_n = 0
            self._twist_fast_n = 0
            return (None, None)

        if self._twist_motion == "frozen":
            if self._twist_fast_n >= TWIST_RESUME_SAMPLES:
                # deliberate twist resumed: re-anchor on the current angle so
                # drift absorbed while frozen never becomes a value jump
                if self.led_mode:
                    v = 50
                elif backend == "brightness":
                    v = read_brightness_pct()
                    if v is None:
                        refresh_brightness()
                        self._twist_baseline = float(deg)
                        return (None, None)  # stay frozen, retry next sample
                else:
                    v = read_volume_pct()
                    if v is None:
                        v = self._twist_last_set
                self._twist_motion = "active"
                self._twist_baseline = float(deg)
                self._twist_v0 = v
                self._twist_last_ui = v
                self._twist_last_set = v
                self._twist_slow_n = 0
                self._twist_fast_n = 0
            else:
                # still frozen: baseline follows the drifting angle
                self._twist_baseline = float(deg)
            return (None, None)

        if self._twist_slow_n >= TWIST_PAUSE_SAMPLES:
            self._twist_motion = "frozen"
            self._twist_baseline = float(deg)
            self._twist_slow_n = 0
            return (None, None)
        if rate is None or abs(rate) <= pause_dps:
            # in-flight still sample: gate closes the leak before freeze trips
            return (None, None)

        delta = float(deg) - self._twist_baseline

        if self._twist_v0 is None:
            if self.led_mode:
                self._twist_v0 = 50    # defensive: the latch path sets this
            elif backend == "brightness":
                # defensive: brightness never falls back to media-key steps
                refresh_brightness()
                return (None, None)
            else:
                # pycaw unavailable -> relative media-key steps
                if delta >= TWIST_STEP_DEG:
                    action = "volume_up"
                elif delta <= -TWIST_STEP_DEG:
                    action = "volume_down"
                else:
                    return (None, None)
                if (now - self._last_fire.get(action, 0.0)) * 1000.0 < VOLUME_REPEAT_MS:
                    return (None, None)
                self._last_fire[action] = now
                self._twist_baseline += TWIST_STEP_DEG if action == "volume_up" else -TWIST_STEP_DEG
                return (action, None)

        full_deg = SWEEP_FULL_DEG if backend == "brightness" else TWIST_FULL_DEG
        target = int(round(self._twist_v0 + delta * (100.0 / full_deg)))
        target = max(0, min(100, target))
        if target == self._twist_last_set:
            return (None, None)
        now = time.monotonic()
        if (now - self._twist_last_set_ts) * 1000.0 < VOLUME_SET_MIN_MS:
            return (None, None)
        if self.led_mode and self._led_sink is not None:
            # LED strip dials: twist = brightness, sweep = hue (0-100 -> 0-359)
            if backend == "brightness":
                self._led_sink("LEDH:%d" % int(round(target * 3.6)))
            else:
                self._led_sink("LEDB:%d" % target)
        elif backend == "brightness":
            if not set_brightness_pct(target):
                return (None, None)
        elif not set_volume_pct(target):
            return (None, None)
        self._twist_last_set = target
        self._twist_last_set_ts = now

        flash: Optional[str] = None
        if self._twist_last_ui is None or abs(target - self._twist_last_ui) >= VOLUME_UI_STEP_PCT:
            up = self._twist_last_ui is None or target > self._twist_last_ui
            if backend == "brightness" or self.led_mode:
                flash = "brightness_up" if up else "brightness_down"
            else:
                flash = "volume_up" if up else "volume_down"
            self._twist_last_ui = target
        return (None, flash)

    def _rot_gate(
        self, entry: Optional[Dict[str, Any]]
    ) -> Optional[Dict[str, Any]]:
        """Pass a pattern match only when its wrist-rotation requirement holds.

        The baseline latches when the pattern first appears, so only rotation
        performed AFTER the bend can fire the gesture (delta is 0 on that
        tick, which blocks the pose for one cycle by construction).
        """
        if entry is None:
            self._rot_ref_id = None
            self._rot_ref_deg = None
            return None
        if entry.get("rot") not in ("cw", "ccw"):
            self._rot_ref_id = None
            self._rot_ref_deg = None
            return entry
        cid = str(entry.get("id", ""))
        deg = self._rot_deg
        if deg is None:
            return None  # gyro silent: gate stays shut
        if self._rot_ref_id != cid:
            self._rot_ref_id = cid
            self._rot_ref_deg = deg
        ref = self._rot_ref_deg
        if ref is None:
            # twist data arrived after the pose formed: first sample becomes
            # the baseline so rotation from before the bend still cannot count
            ref = deg
            self._rot_ref_deg = ref
        delta = deg - ref
        if entry.get("rot") == "cw":
            return entry if delta >= ROT_TRIGGER_DEG else None
        return entry if delta <= -ROT_TRIGGER_DEG else None

    def _tick(self) -> tuple[Optional[str], Optional[str]]:
        if any(b is None for b in self.state):
            return (None, None)
        bent = [bool(b) for b in self.state]
        raw = classify_pose(bent)
        if raw is None:
            entry = self._rot_gate(match_custom(bent, self.custom))
            raw = f"custom:{entry['id']}" if entry else None
        else:
            # built-in pose matched: any latched rot baseline is stale now
            self._rot_ref_id = None
            self._rot_ref_deg = None
        now = time.monotonic()

        if raw != self._candidate:
            self._candidate = raw
            self._candidate_since = now

        pose_changed = False
        if (
            self._candidate is not None
            and self.active_pose != self._candidate
            and (now - self._candidate_since) * 1000.0 >= POSE_STABLE_MS
        ):
            self.active_pose = self._candidate
            self.active_display = self.display_name(self._candidate)
            pose_changed = True
        elif self._candidate is None and self.active_pose is not None:
            if (now - self._candidate_since) * 1000.0 >= POSE_STABLE_MS:
                self.active_pose = None
                self.active_display = None
                pose_changed = True

        if pose_changed and self.active_pose not in ("MID_RING", "RING_ONLY"):
            self._twist_reset()

        action: Optional[str] = None
        act = self._action_for(self.active_pose)
        if act is not None and act != "none":
            elapsed_ms = (now - self._last_fire.get(act, 0.0)) * 1000.0
            if act in MOUSE_NUDGE_ACTIONS:
                # held-pose repeat: nudge every MOUSE_REPEAT_MS while pose active
                if elapsed_ms >= MOUSE_REPEAT_MS:
                    action = act
                    self._last_fire[act] = now
            elif act in VOLUME_ACTIONS or act in BRIGHTNESS_ACTIONS:
                # analog actions repeat while held; toggles (play_pause) stay edge-only
                if elapsed_ms >= VOLUME_REPEAT_MS:
                    action = act
                    self._last_fire[act] = now
            elif pose_changed:
                if elapsed_ms >= COOLDOWN_MS:
                    action = act
                    self._last_fire[act] = now

        return (self.active_display if pose_changed else None), action

# ---------------------------------------------------------------------------
# Media keys
# ---------------------------------------------------------------------------
def fire_media_key(action: str) -> Optional[str]:
    if action in BRIGHTNESS_ACTIONS:
        cur = read_brightness_pct()
        if cur is None:
            refresh_brightness()  # cold cache: prime, no write this tick
            return None
        step = VOLUME_UI_STEP_PCT if action == "brightness_up" else -VOLUME_UI_STEP_PCT
        target = max(0, min(100, cur + step))
        if not set_brightness_pct(target):
            return None
        log.info("brightness: %d%% -> %d%%", cur, target)
        return None
    if action == "mute":
        new_state = toggle_mute()
        if new_state is not None:
            return None
        # pycaw unavailable -> fall through to the blind volumemute key below
    key = ACTION_TO_KEY.get(action)
    nudge = NUDGE_DELTAS.get(action)
    if key is None and nudge is None and action not in ("mouse_click", "mouse_rclick"):
        return None
    try:
        import pyautogui
        pyautogui.FAILSAFE = False
        if nudge is not None:
            pyautogui.moveRel(nudge[0], nudge[1], duration=0)
            log.info("mouse: %s", action)
        elif action == "mouse_click":
            pyautogui.click()
            log.info("mouse: %s", action)
        elif action == "mouse_rclick":
            pyautogui.rightClick()
            log.info("mouse: %s", action)
        elif key is not None:
            pyautogui.press(key)
            log.info("media key: %s -> %s", action, key)
        return None
    except Exception as e:
        return str(e)

# ---------------------------------------------------------------------------
# Absolute volume control (pycaw) — MID_RING twist mapping
# ---------------------------------------------------------------------------
_vol_endpoint: Any = None
_vol_init_done = False


def _get_volume_endpoint() -> Any:
    global _vol_endpoint, _vol_init_done
    if _vol_init_done:
        return _vol_endpoint
    _vol_init_done = True
    try:
        from pycaw.pycaw import AudioUtilities

        spk = AudioUtilities.GetSpeakers()
        if spk is None:
            raise RuntimeError("no default audio render device")
        _vol_endpoint = spk.EndpointVolume
        log.info("pycaw: absolute volume control active")
    except Exception as e:
        _vol_endpoint = None
        log.warning("pycaw unavailable (twist falls back to key steps): %s", e)
    return _vol_endpoint


def read_volume_pct() -> Optional[int]:
    ep = _get_volume_endpoint()
    if ep is None:
        return None
    try:
        return int(round(ep.GetMasterVolumeLevelScalar() * 100.0))
    except Exception:
        return None


def set_volume_pct(pct: int) -> bool:
    ep = _get_volume_endpoint()
    if ep is None:
        return False
    try:
        ep.SetMasterVolumeLevelScalar(max(0, min(100, pct)) / 100.0, None)
        return True
    except Exception as e:
        log.warning("set_volume_pct failed: %s", e)
        return False


def toggle_mute() -> Optional[bool]:
    """True mute/unmute toggle: read the OS mute state, set the opposite.

    Unlike the blind `volumemute` key press, this is state-aware — a second
    pose entry always flips the state even if the first press was swallowed
    or the OS state changed between presses. Returns the NEW state
    (True = muted, False = unmuted) or None when pycaw is unavailable.
    """
    ep = _get_volume_endpoint()
    if ep is None:
        return None
    try:
        cur = bool(ep.GetMute())
        ep.SetMute(not cur, None)
        log.info("mute toggle: %s -> %s", "muted" if cur else "unmuted",
                 "muted" if not cur else "unmuted")
        return not cur
    except Exception as e:
        log.warning("toggle_mute failed: %s", e)
        return None

# ---------------------------------------------------------------------------
# Absolute brightness control (WMI) — RING_ONLY twist mapping
# ---------------------------------------------------------------------------
# Verified on this machine: read (Get-WmiObject -Namespace root\wmi -Class
# WmiMonitorBrightness).CurrentBrightness (101 levels); write
# (Get-WmiObject -Namespace root\wmi -Class WmiMonitorBrightnessMethods)
# .WmiSetBrightness(1, <pct>) — Invoke-WmiMethod -ArgumentList FAILS here, so
# the method call is inlined. A powershell.exe launch takes ~0.2-1s, so ALL
# WMI work runs through brightness_pump() via asyncio.to_thread: the asyncio
# loop (BLE/WS) is never blocked. Writes coalesce newest-wins (twist writes
# every 60ms; stale targets are dropped by the pump).
_bri_lock = threading.Lock()
_bri_cache: Optional[int] = None    # last known % (None = cold, pump not primed)
_bri_target: Optional[int] = None   # newest queued write (coalesced)
_bri_read_now = threading.Event()   # entry-kick: pump should re-read promptly


def read_brightness_pct() -> Optional[int]:
    with _bri_lock:
        return _bri_cache


def refresh_brightness() -> None:
    _bri_read_now.set()  # no-op in effect until the pump is running


def set_brightness_pct(pct: int) -> bool:
    global _bri_cache, _bri_target
    v = max(0, min(100, int(pct)))
    with _bri_lock:
        _bri_target = v      # newest-wins; pump drains at ~50ms cadence
        _bri_cache = v       # optimistic: UI/toasts reflect intent instantly
    return True


def _wmi_read_blocking() -> Optional[int]:
    try:
        out = subprocess.run(
            [
                "powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                "(Get-WmiObject -Namespace root\\wmi -Class "
                "WmiMonitorBrightness).CurrentBrightness",
            ],
            capture_output=True, text=True, timeout=5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        if out.returncode != 0:
            return None
        for line in out.stdout.splitlines():  # multi-monitor may print >1 line
            try:
                return int(float(line.strip()))
            except ValueError:
                continue
        return None
    except Exception:
        return None


def _wmi_write_blocking(pct: int) -> bool:
    v = max(0, min(100, int(pct)))  # int-clamped before interpolation
    try:
        out = subprocess.run(
            [
                "powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                "(Get-WmiObject -Namespace root\\wmi -Class "
                f"WmiMonitorBrightnessMethods).WmiSetBrightness(1, {v})",
            ],
            capture_output=True, text=True, timeout=5,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        return out.returncode == 0
    except Exception:
        return False


async def brightness_pump(stop: asyncio.Event) -> None:
    """Drain queued writes + satisfy read kicks/periodic refresh; never blocks."""
    global _bri_cache, _bri_target
    v = await asyncio.to_thread(_wmi_read_blocking)
    if v is not None:
        with _bri_lock:
            _bri_cache = v
    log.info(
        "brightness: WMI backend %s (%s%%)",
        "active" if v is not None else "unavailable", v,
    )
    last_read = time.monotonic()
    next_periodic = last_read + BRIGHTNESS_REFRESH_S
    _bri_read_now.clear()
    while not stop.is_set():
        with _bri_lock:
            target = _bri_target
            _bri_target = None
        if target is not None:
            ok = await asyncio.to_thread(_wmi_write_blocking, target)
            if not ok:
                log.warning("brightness write failed: %s%%", target)
            continue  # next iteration reconciles reads; keeps write cadence tight
        now = time.monotonic()
        want = _bri_read_now.is_set() or now >= next_periodic
        if want and now - last_read >= 1.0:
            _bri_read_now.clear()  # throttled kicks stay set and retry next loop
            last_read = now
            next_periodic = now + BRIGHTNESS_REFRESH_S
            v = await asyncio.to_thread(_wmi_read_blocking)
            if v is not None:
                with _bri_lock:
                    if _bri_target is None:  # a queued write is newer truth
                        _bri_cache = v
        await asyncio.sleep(0.05)

# ---------------------------------------------------------------------------
# WebSocket hub
# ---------------------------------------------------------------------------
class Hub:
    def __init__(self) -> None:
        self.clients: Set[Any] = set()
        self._lock = asyncio.Lock()

    async def register(self, ws: Any) -> None:
        async with self._lock:
            self.clients.add(ws)

    async def unregister(self, ws: Any) -> None:
        async with self._lock:
            self.clients.discard(ws)

    async def broadcast(self, msg: Dict[str, Any]) -> None:
        if not self.clients:
            return
        data = json.dumps(msg)
        dead = []
        async with self._lock:
            targets = list(self.clients)
        for ws in targets:
            try:
                await ws.send(data)
            except Exception:
                dead.append(ws)
        for ws in dead:
            await self.unregister(ws)

# ---------------------------------------------------------------------------
# Bridge core
# ---------------------------------------------------------------------------
class Bridge:
    def __init__(self, device_name: str, simulate: bool) -> None:
        self.device_name = device_name
        self.simulate = simulate
        self.hub = Hub()
        cfg = load_config()
        self.bindings = cfg["bindings"]
        self.thresholds = cfg["thresholds"]
        self.custom = cfg["custom"]
        self.engine = GestureEngine(self.bindings, self.thresholds, self.custom)
        self.engine._led_sink = self._send_ble
        self.connected = False
        self.device_label = device_name
        self._rx_buf = b""
        self._client: Any = None  # bleak client
        self._connected_evt = asyncio.Event()
        self._stop = asyncio.Event()
        self.last_pct: List[int] = [0, 0, 0, 0, 0]

    def config_msg(self) -> Dict[str, Any]:
        return {
            "type": "config",
            "bindings": self.bindings,
            "thresholds": self.thresholds,
            "custom": self.custom,
        }

    async def _emit_pose_action(
        self, pose: Optional[str], action: Optional[str]
    ) -> None:
        if pose is not None:
            await self.hub.broadcast({"type": "pose", "name": pose})
        if action is not None:
            await self.hub.broadcast(
                {"type": "action", "action": action, "gesture": self.engine.active_display or ""}
            )
            if self.engine.led_mode:
                # LED mode: gestures drive the strip over BLE; unmapped
                # actions (mouse moves, stop) have no strip equivalent
                cmd = LED_ACTION_MAP.get(action)
                if cmd:
                    self._send_ble(cmd)
            else:
                err = fire_media_key(action)
                if err:
                    await self.hub.broadcast(
                        {"type": "log", "level": "error", "msg": f"media key failed: {err}"}
                    )

    def _send_ble(self, text: str) -> None:
        client = self._client
        if client is None or not getattr(client, "is_connected", False):
            return
        try:
            asyncio.ensure_future(
                client.write_gatt_char(NUS_RX_CHAR, text.encode("utf-8"), response=False)
            )
        except Exception as e:
            log.debug("ble write failed: %s", e)

    async def _apply_glove_thresholds(self, line: str) -> None:
        # the glove owns the percent bands: its NVS copy is the shared truth, so
        # a value set on the on-board portal must win over config.json
        tag, _, body = line.partition(":")
        try:
            vals = [int(float(x)) for x in body.split(",")[:5]]
        except ValueError:
            return
        if len(vals) < 5:
            return
        key = "on" if tag == "THR" else "off"
        incoming = {name: {key: vals[i]} for i, name in enumerate(FINGER_ORDER)}
        merged = _clean_thresholds(incoming, self.thresholds)
        if merged == self.thresholds:
            return
        self.thresholds = merged
        self.engine.set_thresholds(self.thresholds)
        save_config(
            {
                "bindings": self.bindings,
                "thresholds": self.thresholds,
                "custom": self.custom,
            }
        )
        await self.hub.broadcast(self.config_msg())
        log.info("glove %s bands applied: %s", key, vals)

    async def _apply_glove_twist(self, line: str) -> None:
        try:
            pause_s, resume_s = line.split(":", 1)[1].split(",")[:2]
            pause, resume = float(pause_s), float(resume_s)
        except (ValueError, IndexError):
            return
        if not 0.0 < pause < resume:
            log.warning("ignoring glove twist gates pause=%.1f resume=%.1f", pause, resume)
            return
        self.engine.twist_pause_dps = pause
        self.engine.twist_resume_dps = resume
        log.info("glove twist gates: pause=%.1f resume=%.1f", pause, resume)

    def push_glove_thresholds(self) -> None:
        on = [self.thresholds[n]["on"] for n in FINGER_ORDER]
        off = [self.thresholds[n]["off"] for n in FINGER_ORDER]
        self._send_ble("SO:" + ",".join(str(v) for v in on))
        self._send_ble("SF:" + ",".join(str(v) for v in off))

    async def handle_fingers_line(self, line: str) -> None:
        line = line.strip()
        if not line:
            return
        if line.startswith("THR:") or line.startswith("THO:"):
            await self._apply_glove_thresholds(line)
            return
        if line.startswith("TWD:"):
            await self._apply_glove_twist(line)
            return
        if line.startswith("FINGERS:"):
            try:
                parts = line.split(":", 1)[1].split(",")
                pct = [int(float(x)) for x in parts[:5]]
                if len(pct) < 5:
                    return
            except ValueError:
                return
            self.last_pct = pct
            pose, action = self.engine.on_fingers(pct)
            await self.hub.broadcast(
                {
                    "type": "fingers",
                    "pct": pct,
                    "bent": self.engine.bent_flags(),
                    "twist": self.engine._rot_deg,
                }
            )
            await self._emit_pose_action(pose, action)
        elif line.startswith("FINGER:"):
            rest = line.split(":", 1)[1]
            if ":" not in rest:
                return
            name, status = rest.split(":", 1)
            status = status.strip().upper()
            if status not in ("BENT", "EXT"):
                return
            pose, action = self.engine.on_finger_edge(name, status == "BENT")
            await self.hub.broadcast(
                {
                    "type": "fingers",
                    "pct": self.last_pct,
                    "bent": self.engine.bent_flags(),
                    "twist": self.engine._rot_deg,
                }
            )
            await self._emit_pose_action(pose, action)
        elif line.startswith("TWIST:"):
            try:
                deg = TWIST_INPUT_SIGN * float(line.split(":", 1)[1])
            except ValueError:
                return
            key_action, flash = self.engine.on_twist(deg)
            await self.hub.broadcast(
                {
                    "type": "fingers",
                    "pct": self.last_pct,
                    "bent": self.engine.bent_flags(),
                    "twist": self.engine._rot_deg,
                }
            )
            if flash is not None:
                await self.hub.broadcast(
                    {"type": "action", "action": flash, "gesture": self.engine.active_display or ""}
                )
            if key_action is not None:
                await self._emit_pose_action(None, key_action)
            # a twist can satisfy a rot gate this instant; tick here so the
            # gesture fires now instead of waiting for the next FINGERS line
            pose, action = self.engine._tick()
            await self._emit_pose_action(pose, action)
        elif line.startswith("SWEEP:"):
            try:
                deg = SWEEP_INPUT_SIGN * float(line.split(":", 1)[1])
            except ValueError:
                return
            key_action, flash = self.engine.on_sweep(deg)
            await self.hub.broadcast(
                {
                    "type": "fingers",
                    "pct": self.last_pct,
                    "bent": self.engine.bent_flags(),
                    "twist": self.engine._rot_deg,
                }
            )
            if flash is not None:
                await self.hub.broadcast(
                    {"type": "action", "action": flash, "gesture": self.engine.active_display or ""}
                )
            if key_action is not None:
                await self._emit_pose_action(None, key_action)
        elif line.startswith("GESTURE:"):
            await self.hub.broadcast(
                {"type": "status", "cal": "OK", "text": line.split(":", 1)[1].strip()}
            )
        elif "NEED_CALIBRATION" in line:
            await self.hub.broadcast(
                {"type": "status", "cal": "NEED_CALIBRATION", "text": line}
            )
        elif line.startswith("T:"):
            await self.hub.broadcast(
                {"type": "status", "cal": "OK", "text": line}
            )
        elif line.startswith("MOD:"):
            try:
                m = int(line.split(":", 1)[1].strip())
            except ValueError:
                return
            self.engine.led_mode = (m == 1)
            await self.hub.broadcast({"type": "mode", "ctl": m})

    async def set_connection(self, up: bool, label: Optional[str] = None) -> None:
        self.connected = up
        if label:
            self.device_label = label
        await self.hub.broadcast(
            {"type": "connection", "connected": up, "device": self.device_label}
        )

    # ---- BLE path ---------------------------------------------------------
    async def run_ble(self) -> None:
        try:
            from bleak import BleakClient, BleakScanner
        except ImportError:
            log.error("bleak not installed")
            return

        def on_disconnect(_c: Any) -> None:
            log.warning("BLE disconnected")
            self._connected_evt.clear()

        while not self._stop.is_set():
            try:
                log.info("scanning for %s ...", self.device_name)
                await self.set_connection(False)
                dev = await BleakScanner.find_device_by_name(
                    self.device_name, timeout=10.0
                )
                if dev is None:
                    log.warning("device not found, retrying in 3s")
                    await asyncio.sleep(3.0)
                    continue

                async with BleakClient(
                    dev, disconnected_callback=on_disconnect
                ) as client:
                    self._client = client
                    log.info("connected to %s (%s)", dev.name, dev.address)
                    await self.set_connection(True, f"{dev.name} ({dev.address})")

                    def _cb(_chr: Any, data: bytearray) -> None:
                        # bleak callback — schedule on loop
                        try:
                            asyncio.get_event_loop().call_soon_threadsafe(
                                self._feed_bytes, bytes(data)
                            )
                        except RuntimeError:
                            pass

                    await client.start_notify(NUS_TX_CHAR, _cb)
                    try:
                        await client.write_gatt_char(
                            NUS_RX_CHAR, b"GETCFG", response=False
                        )
                    except Exception as e:
                        log.debug("GETCFG failed: %s", e)
                    self._send_ble("GETMODE")
                    while client.is_connected and not self._stop.is_set():
                        await asyncio.sleep(0.5)
                    await self.set_connection(False)
            except Exception as e:
                log.error("BLE error: %s", e)
                await self.hub.broadcast(
                    {"type": "log", "level": "error", "msg": f"BLE: {e}"}
                )
                await self.set_connection(False)
            finally:
                self._client = None
            if not self._stop.is_set():
                await asyncio.sleep(2.0)

    def _feed_bytes(self, data: bytes) -> None:
        self._rx_buf += data
        while b"\n" in self._rx_buf:
            line, self._rx_buf = self._rx_buf.split(b"\n", 1)
            try:
                text = line.decode("utf-8", errors="replace")
            except Exception:
                continue
            asyncio.ensure_future(self.handle_fingers_line(text))

    # ---- simulate path ----------------------------------------------------
    async def run_simulate(self) -> None:
        """Cycle through poses so the UI can be exercised without hardware."""
        await asyncio.sleep(0.8)
        await self.set_connection(True, "SIMULATED-Glove")
        # (pct, hold_seconds, twist_span_deg or None); pct 100=bent, 0=extended
        program: List[tuple[List[int], float, Optional[int]]] = [
            ([0, 0, 0, 0, 0], 1.2, None),
            ([100, 100, 100, 100, 100], 1.5, None),
            ([0, 0, 100, 100, 100], 1.5, None),
            ([0, 0, 0, 100, 100], 1.5, None),
            ([0, 100, 100, 100, 100], 1.5, None),
            ([100, 0, 0, 0, 0], 1.5, 90),   # THUMB_BENT: twist must be IGNORED now
            ([0, 0, 100, 100, 0], 3.2, 200),  # MID_RING: absolute volume ±200°
            ([0, 0, 0, 100, 0], 3.2, 200),  # RING_ONLY: sweep drives brightness ±200°
            ([0, 100, 0, 100, 100], 1.5, None),
            ([100, 100, 100, 100, 0], 1.5, None),
            ([0, 0, 0, 0, 0], 1.2, None),
        ]
        prev_bent = [False] * 5
        idx = 0
        while not self._stop.is_set():
            pct, hold, twist_span = program[idx % len(program)]
            idx += 1
            t0 = time.monotonic()
            while time.monotonic() - t0 < hold and not self._stop.is_set():
                j = [max(0, min(100, v + random.randint(-3, 3))) for v in pct]
                await self.handle_fingers_line(
                    "FINGERS:" + ",".join(str(x) for x in j)
                )
                bent = [
                    v >= int(self.engine.thresholds[FINGER_ORDER[bidx]]["on"])
                    for bidx, v in enumerate(j)
                ]
                for fname, fi in FINGER_NAME_TO_IDX.items():
                    if bent[fi] != prev_bent[fi]:
                        prev_bent[fi] = bent[fi]
                        await self.handle_fingers_line(
                            f"FINGER:{fname.capitalize()}:{'BENT' if bent[fi] else 'EXT'}"
                        )
                if twist_span:
                    frac = (time.monotonic() - t0) / hold
                    tri = 1.0 - abs(2.0 * frac - 1.0)
                    v = int(twist_span * (2.0 * tri - 1.0))
                    await self.handle_fingers_line(f"TWIST:{v}")
                    await self.handle_fingers_line(f"SWEEP:{v}")
                await asyncio.sleep(0.15)
            await asyncio.sleep(0.2)

    # ---- WebSocket server -------------------------------------------------
    async def ws_handler(self, ws: Any) -> None:
        await self.hub.register(ws)
        try:
            # greet
            await ws.send(
                json.dumps(
                    {
                        "type": "connection",
                        "connected": self.connected,
                        "device": self.device_label,
                    }
                )
            )
            await ws.send(json.dumps(self.config_msg()))
            async for raw in ws:
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    continue
                mtype = msg.get("type")
                if mtype == "ping":
                    await ws.send(json.dumps({"type": "pong"}))
                elif mtype == "request_config":
                    await ws.send(json.dumps(self.config_msg()))
                elif mtype == "set_config":
                    # subset semantics: keys absent from the message keep current values
                    if isinstance(msg.get("bindings"), dict):
                        self.bindings = _clean_bindings(msg["bindings"], self.bindings)
                    if isinstance(msg.get("thresholds"), dict):
                        self.thresholds = _clean_thresholds(
                            msg["thresholds"], self.thresholds
                        )
                    if "custom" in msg:
                        self.custom = _clean_custom(msg.get("custom"))
                    self.engine.set_bindings(self.bindings)
                    self.engine.set_thresholds(self.thresholds)
                    self.engine.set_custom(self.custom)
                    save_config(
                        {
                            "bindings": self.bindings,
                            "thresholds": self.thresholds,
                            "custom": self.custom,
                        }
                    )
                    if isinstance(msg.get("thresholds"), dict):
                        self.push_glove_thresholds()
                    await self.hub.broadcast(self.config_msg())
                # unknown types ignored
        except Exception as e:
            log.debug("ws closed: %s", e)
        finally:
            await self.hub.unregister(ws)

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
async def main() -> None:
    parser = argparse.ArgumentParser(description="QD023 glove bridge")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--device-name", default="QD023-Glove")
    parser.add_argument("--simulate", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    try:
        import websockets
    except ImportError:
        log.error("websockets not installed")
        sys.exit(1)

    bridge = Bridge(args.device_name, args.simulate)
    bri = asyncio.create_task(brightness_pump(bridge._stop))

    async with websockets.serve(
        bridge.ws_handler, args.host, args.port, ping_interval=20, ping_timeout=20
    ):
        log.info(
            "WS listening on ws://%s:%d  (simulate=%s)",
            args.host, args.port, args.simulate,
        )
        if args.simulate:
            worker = asyncio.create_task(bridge.run_simulate())
        else:
            worker = asyncio.create_task(bridge.run_ble())
        try:
            await worker
        except asyncio.CancelledError:
            pass
        finally:
            bridge._stop.set()
            try:
                await asyncio.wait_for(bri, timeout=6.0)
            except asyncio.TimeoutError:
                bri.cancel()
                log.debug("brightness pump did not stop in time")
            except Exception as e:
                log.debug("brightness pump ended: %s", e)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("bye")
