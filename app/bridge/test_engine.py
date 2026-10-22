#!/usr/bin/env python3
"""Engine smoke: thresholds, custom gestures, mouse actions, config merge."""
from __future__ import annotations

import sys
import time
import types
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import glove_bridge as gb

passed = 0


def check(label: str, cond: bool) -> None:
    global passed
    assert cond, label
    passed += 1
    print(f"ok - {label}")


def settle(e: gb.GestureEngine, state: list) -> tuple:
    e.state = list(state)
    e._tick()
    time.sleep(0.16)
    return e._tick()


# --- group 1: defaults -------------------------------------------------
cfg = gb.load_config()
check("default thumb on=56", cfg["thresholds"]["thumb"]["on"] == 56)
check("default thumb off=30", cfg["thresholds"]["thumb"]["off"] == 30)
check("default index on=46", cfg["thresholds"]["index"]["on"] == 46)
check("default index off=18", cfg["thresholds"]["index"]["off"] == 18)
check("default bindings cover all gestures", set(cfg["bindings"]) == set(gb.GESTURE_IDS))
check("custom default empty", cfg["custom"] == [])

# --- group 2: validation ------------------------------------------------
th = gb._clean_thresholds(
    {"index": {"on": 40}, "thumb": {"on": 5, "off": 999}}, gb.DEFAULT_THRESHOLDS
)
check("on clamped low to 15", th["thumb"]["on"] == 15)
check("off clamped to on-5", th["thumb"]["off"] == 10)
check("valid on accepted (40)", th["index"]["on"] == 40)
check("index off kept default (18)", th["index"]["off"] == 18)
check("missing fingers keep defaults", th["middle"]["on"] == 46)
th2 = gb._clean_thresholds({"pinky": {"on": 200, "off": "x"}}, gb.DEFAULT_THRESHOLDS)
check("on clamped high to 95", th2["pinky"]["on"] == 95)
check("unparseable off keeps default", th2["pinky"]["off"] == 18)

custom = gb._clean_custom(
    [
        {"name": "Gun", "pattern": ["b", "e", "e", "x", "x"], "action": "mouse_right"},
        {"id": "bad1", "name": "X", "pattern": ["B", "E"], "action": "none"},
        {"id": "bad2", "name": "Y", "pattern": ["B", "E", "Z", "X", "X"], "action": "none"},
        {"id": "ok3", "name": "", "pattern": ["X", "X", "X", "X", "X"], "action": "jump"},
    ]
)
check("invalid patterns dropped", len(custom) == 2)
check("missing id generated", isinstance(custom[0]["id"], str) and len(custom[0]["id"]) > 0)
check("pattern uppercased", custom[0]["pattern"][:3] == ["B", "E", "E"])
check("unknown action -> none", custom[1]["action"] == "none")
check("empty name -> Custom", custom[1]["name"] == "Custom")
many = gb._clean_custom(
    [{"id": f"i{n}", "name": "n", "pattern": ["X"] * 5, "action": "none"} for n in range(20)]
)
check("capped at 12", len(many) == 12)
dup = gb._clean_custom(
    [
        {"id": "d1", "name": "old", "pattern": ["B", "E", "E", "E", "E"], "action": "none"},
        {"id": "d1", "name": "new", "pattern": ["B", "E", "E", "E", "E"], "action": "next"},
    ]
)
check(
    "duplicate id last wins",
    len(dup) == 1 and dup[0]["name"] == "new" and dup[0]["action"] == "next",
)

# --- group 3: precedence ------------------------------------------------
gun = {"id": "c1", "name": "Gun", "pattern": ["B", "B", "E", "X", "X"], "action": "mouse_right"}
mirror = {"id": "c2", "name": "MirrorFist", "pattern": ["B", "B", "B", "B", "B"], "action": "mouse_left"}
e = gb.GestureEngine(
    dict(gb.DEFAULT_BINDINGS),
    {k: dict(v) for k, v in gb.DEFAULT_THRESHOLDS.items()},
    [mirror, gun],
)
pose, act = settle(e, [True, True, True, True, True])
check("built-in FIST wins over custom mirror", pose == "FIST")
check("FIST action from bindings", act == "play_pause")

e2 = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS), custom=[gun])
pose, act = settle(e2, [True, True, False, False, False])
check("custom pose fires when no built-in matches", pose == "Gun")
check("custom action used", act == "mouse_right")
check("active_pose is key form", e2.active_pose == "custom:c1")

# --- group 4: mouse actions ---------------------------------------------
fake = types.ModuleType("pyautogui")
calls = {"moveRel": [], "click": 0, "rightClick": 0, "press": []}


def _move(dx, dy, duration=0):
    calls["moveRel"].append((dx, dy))


def _click(*a, **k):
    calls["click"] += 1


def _rclick(*a, **k):
    calls["rightClick"] += 1


def _press(k):
    calls["press"].append(k)


fake.__dict__.update(
    {
        "FAILSAFE": True,
        "moveRel": _move,
        "click": _click,
        "rightClick": _rclick,
        "press": _press,
    }
)
sys.modules["pyautogui"] = fake

check("mouse_left no error", gb.fire_media_key("mouse_left") is None)
check("nudge -60 x", calls["moveRel"][-1] == (-60, 0))
gb.fire_media_key("mouse_up")
check("nudge -60 y", calls["moveRel"][-1] == (0, -60))
gb.fire_media_key("mouse_click")
check("left click", calls["click"] == 1)
gb.fire_media_key("mouse_rclick")
check("right click", calls["rightClick"] == 1)
gb.fire_media_key("play_pause")
check("media key press", calls["press"] == ["playpause"])

# --- group 5: subset merge ----------------------------------------------
cur = {
    "bindings": dict(gb.DEFAULT_BINDINGS),
    "thresholds": {k: dict(v) for k, v in gb.DEFAULT_THRESHOLDS.items()},
    "custom": [gun],
}
b = gb._clean_bindings({}, cur["bindings"])
check("empty bindings keeps current", b == cur["bindings"])
b2 = gb._clean_bindings({"FIST": "not_an_action"}, cur["bindings"])
check("unknown action in bindings -> none", b2["FIST"] == "none")
check("other bindings kept", b2["INDEX_UP"] == cur["bindings"]["INDEX_UP"])
t = gb._clean_thresholds({"ring": {"on": 40}}, cur["thresholds"])
check("subset thresholds merge", t["ring"]["on"] == 40 and t["thumb"]["on"] == 56)

e3 = gb.GestureEngine(
    dict(gb.DEFAULT_BINDINGS),
    {k: dict(v) for k, v in gb.DEFAULT_THRESHOLDS.items()},
    [],
)
e3.set_thresholds(t)
check("engine thresholds updated", e3.thresholds["ring"]["on"] == 40)
e3.set_custom([gun])
check("engine custom updated", len(e3.custom) == 1)

e4 = gb.GestureEngine(
    dict(gb.DEFAULT_BINDINGS),
    gb._clean_thresholds({"index": {"on": 40}}, gb.DEFAULT_THRESHOLDS),
)
e4.on_fingers([0, 40, 0, 0, 0])
check("index pct 40 counts bent at on=40", e4.state[1] is True)
e5 = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS))
e5.on_fingers([0, 40, 0, 0, 0])
check("index pct 40 not bent at default 46", e5.state[1] is False)

# --- group 6: pose display name -----------------------------------------
check("display name for built-in", e2.display_name("FIST") == "FIST")
check("display name for custom", e2.display_name("custom:c1") == "Gun")
check("display name for unknown custom key", e2.display_name("custom:zzz") == "Custom")

# --- group 7: rotation-gated custom gestures ----------------------------
rcfg = gb._clean_custom(
    [
        {"id": "rw1", "name": "SpinR", "pattern": ["B", "B", "E", "X", "X"],
         "action": "next", "rot": "cw"},
        {"id": "rw2", "name": "SpinL", "pattern": ["E", "B", "B", "X", "X"],
         "action": "prev", "rot": "ccw"},
        {"id": "rw3", "name": "Bad", "pattern": ["E", "E", "E", "E", "E"],
         "action": "stop", "rot": "diagonal"},
        {"id": "rw4", "name": "Plain", "pattern": ["B", "B", "B", "X", "X"],
         "action": "mute"},
    ]
)
check("rot cw kept", rcfg[0]["rot"] == "cw")
check("rot ccw kept", rcfg[1]["rot"] == "ccw")
check("invalid rot -> none", rcfg[2]["rot"] == "none")
check("missing rot -> none", rcfg[3]["rot"] == "none")

er = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS), custom=[rcfg[0]])
pose, act = settle(er, [True, True, False, False, False])
check("rot=cw: pattern alone no fire (no twist data)", pose is None and er.active_pose is None)

er.on_twist(0.0)
pose, act = settle(er, [True, True, False, False, False])
check("rot=cw: 0 deg no fire", pose is None)

er.on_twist(30.0)
pose, act = settle(er, [True, True, False, False, False])
check("rot=cw: +30 deg below threshold no fire", pose is None and er.active_pose is None)

er.on_twist(50.0)
pose, act = settle(er, [True, True, False, False, False])
check("rot=cw: +50 deg fires", pose == "SpinR" and act == "next")

pose, act = settle(er, [False, False, False, False, False])
check("rot=cw: release -> OPEN_PALM", pose == "OPEN_PALM")

pose, act = settle(er, [True, True, False, False, False])
check("rot=cw: rebend at same angle no fire (relatched)", pose is None and er.active_pose is None)

er.on_twist(110.0)
pose, act = settle(er, [True, True, False, False, False])
check("rot=cw: further rotation re-fires", pose == "SpinR" and act == "next")

el = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS), custom=[rcfg[1]])
el.on_twist(0.0)
pose, act = settle(el, [False, True, True, False, False])
check("rot=ccw: pattern alone no fire", pose is None and el.active_pose is None)

el.on_twist(-30.0)
pose, act = settle(el, [False, True, True, False, False])
check("rot=ccw: -30 deg below threshold no fire", pose is None)

el.on_twist(-50.0)
pose, act = settle(el, [False, True, True, False, False])
check("rot=ccw: -50 deg fires", pose == "SpinL" and act == "prev")

ep = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS), custom=[rcfg[3]])
pose, act = settle(ep, [True, True, True, False, False])
check("rot=none fires without any twist", pose == "Plain" and act == "mute")

# --- group 8: held-pose volume repeat (analog vs toggle) -----------------
ev = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS))
pose, act = settle(ev, [False, True, False, True, True])  # MIDDLE_UP: middle extended
check("MIDDLE_UP entry fires volume_down", act == "volume_down")
pose, act = ev._tick()
check("volume held inside 250ms no repeat", act is None)
time.sleep(0.26)
pose, act = ev._tick()
check("volume held past 250ms repeats", act == "volume_down")

ef = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS))
pose, act = settle(ef, [True, True, True, True, True])
check("FIST entry fires play_pause", act == "play_pause")
time.sleep(0.26)
pose, act = ef._tick()
check("FIST held does not repeat play_pause", act is None)

# --- group 9: hold-freeze motion FSM (absolute twist volume) -------------
# monkeypatch pycaw helpers: tests must never touch the real system volume
_orig_read, _orig_set = gb.read_volume_pct, gb.set_volume_pct
_fake_vol = {"pct": 50}
_writes: list = []
gb.read_volume_pct = lambda: _fake_vol["pct"]


def _fake_set(target: int) -> bool:
    _writes.append(target)
    _fake_vol["pct"] = target
    return True


gb.set_volume_pct = _fake_set

em = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS))
pose, _act = settle(em, [False, False, True, True, False])
check("MID_RING pose settles", em.active_pose == "MID_RING")

r = em.on_twist(0.0)
check("pose-entry latch writes nothing", _writes == [] and r == (None, None))

time.sleep(0.1)
em.on_twist(3.0)  # 30 deg/s gyro drift while wrist held still
time.sleep(0.1)
em.on_twist(6.0)
check("frozen drift writes nothing", _writes == [] and em._twist_motion == "frozen")

time.sleep(0.05)
em.on_twist(14.0)  # 160 deg/s deliberate twist, sample 1 of 2 to resume
check("single fast sample stays frozen (no write)", _writes == [] and em._twist_motion == "frozen")
time.sleep(0.05)
r = em.on_twist(22.0)  # sample 2 -> resume re-anchors on current angle
check("second fast sample resumes without jump write",
      _writes == [] and r == (None, None) and em._twist_motion == "active")

time.sleep(0.05)
em.on_twist(30.0)  # delta 8 deg from resume anchor -> 50 + 4.4 -> 54
check("twist after resume writes target", _writes == [54])

time.sleep(0.1)
em.on_twist(33.0)  # wrist stops: 30 deg/s -> rate-gated before freeze trips
check("first still sample gated (no write)", _writes == [54])
time.sleep(0.1)
em.on_twist(36.0)  # second still sample -> freeze
check("second still sample freezes volume",
      _writes == [54] and em._twist_motion == "frozen")

time.sleep(0.1)
em.on_twist(39.0)
time.sleep(0.1)
em.on_twist(42.0)
check("hold drift after freeze writes nothing", _writes == [54])

time.sleep(0.05)
em.on_twist(50.0)
time.sleep(0.05)
r = em.on_twist(58.0)  # resume re-anchors at current drift angle -> v0 = 54
check("second hold cycle resumes without jump write",
      _writes == [54] and r == (None, None) and em._twist_v0 == 54)

time.sleep(0.05)
em.on_twist(66.0)  # delta 8 deg -> 54 + 4.4 -> 58
check("twist after second resume writes next target", _writes == [54, 58])

check("MID_RING binding untouched by FSM",
      em.bindings["MID_RING"] == "none")

gb.read_volume_pct, gb.set_volume_pct = _orig_read, _orig_set

# --- group 10: RING_ONLY brightness sweep ---------------------------------
check("RING_ONLY classify", gb.classify_pose([False, False, False, True, False]) == "RING_ONLY")
check("RING_ONLY thumb ignored", gb.classify_pose([True, False, False, True, False]) == "RING_ONLY")
check("RING_ONLY disjoint from PEACE", gb.classify_pose([False, False, False, True, True]) == "PEACE")
check("RING_ONLY disjoint from MID_RING", gb.classify_pose([False, False, True, True, False]) == "MID_RING")
check("RING_ONLY in GESTURE_IDS", "RING_ONLY" in gb.GESTURE_IDS)
check("brightness actions defined",
      gb.ACTION_IDS.count("brightness_up") == 1
      and gb.ACTION_IDS.count("brightness_down") == 1
      and gb.BRIGHTNESS_ACTIONS == {"brightness_up", "brightness_down"})
check("RING_ONLY default binding none", gb.DEFAULT_BINDINGS["RING_ONLY"] == "none")
_bclean = gb._clean_bindings(
    {"FIST": "brightness_up", "RING_ONLY": "brightness_up"}, gb.DEFAULT_BINDINGS
)
check("brightness action survives binding clean", _bclean["FIST"] == "brightness_up")
check("RING_ONLY bindable", _bclean["RING_ONLY"] == "brightness_up")

# monkeypatch BOTH backends: tests must never touch the real system
_o_br, _o_bw, _o_rf = gb.read_brightness_pct, gb.set_brightness_pct, gb.refresh_brightness
_o_vr, _o_vs = gb.read_volume_pct, gb.set_volume_pct
_fake_bri = {"pct": 40, "cold": False}
_bri_writes: list = []
_bri_refreshes = {"n": 0}
_vol_reads = {"n": 0}
_vol_writes: list = []


def _fake_bri_read():
    return None if _fake_bri["cold"] else _fake_bri["pct"]


def _fake_bri_write(target: int) -> bool:
    _bri_writes.append(target)
    _fake_bri["pct"] = target
    return True


def _fake_bri_refresh():
    _bri_refreshes["n"] += 1


def _fake_vol_read():
    _vol_reads["n"] += 1
    return 77


def _fake_vol_write(target: int) -> bool:
    _vol_writes.append(target)
    return True


gb.read_brightness_pct = _fake_bri_read
gb.set_brightness_pct = _fake_bri_write
gb.refresh_brightness = _fake_bri_refresh
gb.read_volume_pct = _fake_vol_read
gb.set_volume_pct = _fake_vol_write

eb = gb.GestureEngine(dict(gb.DEFAULT_BINDINGS))
pose, _act = settle(eb, [False, False, False, True, False])
check("RING_ONLY pose settles", eb.active_pose == "RING_ONLY")

r = eb.on_sweep(0.0)
check("brightness entry latches, no volume read",
      r == (None, None) and _vol_reads["n"] == 0 and _bri_writes == []
      and eb._twist_v0 == 40 and eb._twist_backend == "brightness")

time.sleep(0.1)
eb.on_sweep(1.0)
time.sleep(0.1)
eb.on_sweep(2.0)
check("brightness frozen drift writes nothing",
      _bri_writes == [] and eb._twist_motion == "frozen")

time.sleep(0.05)
eb.on_sweep(4.0)
check("brightness single fast sample stays frozen",
      _bri_writes == [] and eb._twist_motion == "frozen")
time.sleep(0.05)
r = eb.on_sweep(6.0)
check("brightness resume no jump write",
      _bri_writes == [] and r == (None, None) and eb._twist_motion == "active")

time.sleep(0.05)
eb.on_sweep(15.0)
check("brightness sweep writes target", _bri_writes == [50])
check("no volume write during brightness sweep", _vol_writes == [])

time.sleep(0.07)
r = eb.on_sweep(24.0)
check("brightness flash is brightness_up",
      r == (None, "brightness_up") and _bri_writes == [50, 60])
time.sleep(0.07)
r = eb.on_sweep(15.0)
check("brightness flash is brightness_down",
      r == (None, "brightness_down") and _bri_writes == [50, 60, 50])

pose, _act = settle(eb, [False, False, True, True, False])
check("switch to MID_RING settles", eb.active_pose == "MID_RING")
r = eb.on_twist(100.0)
check("MID_RING entry re-anchors on volume backend",
      r == (None, None) and eb._twist_v0 == 77 and eb._twist_backend == "volume")
check("volume read used on switch", _vol_reads["n"] == 1)
check("brightness writes unchanged by switch", _bri_writes == [50, 60, 50])

pose, _act = settle(eb, [False, False, False, True, False])
check("back to RING_ONLY settles", eb.active_pose == "RING_ONLY")
_bri_before = list(_bri_writes)
_vol_before = _vol_reads["n"]
_bri_refreshes["n"] = 0
_fake_bri["cold"] = True
r = eb.on_sweep(200.0)
check("cold entry kicks refresh, latches nothing",
      r == (None, None) and _bri_refreshes["n"] == 1 and eb._twist_baseline is None)
check("cold entry never falls back to volume",
      _vol_reads["n"] == _vol_before and _bri_writes == _bri_before)
r = eb.on_sweep(203.0)
check("cold entry retries every sample",
      _bri_refreshes["n"] == 2 and eb._twist_baseline is None)
_fake_bri["cold"] = False
r = eb.on_sweep(206.0)
check("warm retry latches brightness", eb._twist_v0 == 50 and r == (None, None))

# channel guards: each input must only drive its own pose's backend
_bri_ref = list(_bri_writes)
_fsm_ref = (eb._twist_prev_deg, eb._twist_baseline, eb._twist_backend, eb._twist_motion)
r = eb.on_twist(999.0)
check("on_twist ignores RING_ONLY (sweep owns it)",
      r == (None, None) and _bri_writes == _bri_ref
      and eb._rot_deg == 999.0
      and (eb._twist_prev_deg, eb._twist_baseline, eb._twist_backend, eb._twist_motion) == _fsm_ref)
settle(eb, [False, False, True, True, False])
_vol_ref = _vol_reads["n"]
_vw_ref = list(_vol_writes)
_m_ref = (eb._twist_baseline, eb._twist_backend, eb._twist_motion)
r = eb.on_sweep(500.0)
check("on_sweep ignores MID_RING (twist owns it)",
      r == (None, None) and _vol_reads["n"] == _vol_ref
      and _vol_writes == _vw_ref
      and (eb._twist_baseline, eb._twist_backend, eb._twist_motion) == _m_ref)
settle(eb, [False, False, False, False, False])
r = eb.on_twist(0.0)
check("twist heartbeat outside a twist pose clears FSM",
      r == (None, None) and eb._twist_baseline is None and eb._twist_backend is None)
settle(eb, [False, False, False, True, False])
eb.twist_pause_dps = 1.0e6
eb.twist_resume_dps = 1.0e6
r = eb.on_sweep(0.0)
check("guard entry frozen", r == (None, None) and eb._twist_motion == "frozen")
time.sleep(0.05)
eb.on_sweep(2.0)
time.sleep(0.05)
eb.on_sweep(4.0)
check("sweep resume ignores inflated twist thresholds",
      eb._twist_motion == "active")
_n_ref = len(_bri_writes)
time.sleep(0.05)
r = eb.on_sweep(13.0)
check("sweep writes with twist thresholds sabotaged",
      len(_bri_writes) == _n_ref + 1 and _bri_writes[-1] == 60)

_bri_writes.clear()
_fake_bri["pct"] = 95
r = gb.fire_media_key("brightness_up")
check("fire brightness_up steps +10 clamped", _bri_writes == [100] and r is None)
_bri_writes.clear()
_fake_bri["pct"] = 5
r = gb.fire_media_key("brightness_down")
check("fire brightness_down steps -10 clamped", _bri_writes == [0] and r is None)
_bri_writes.clear()
_bri_refreshes["n"] = 0
_fake_bri["cold"] = True
r = gb.fire_media_key("brightness_up")
check("fire cold cache kicks refresh, no write",
      _bri_writes == [] and _bri_refreshes["n"] == 1 and r is None)
_fake_bri["cold"] = False

ebind = gb.GestureEngine({**dict(gb.DEFAULT_BINDINGS), "FIST": "brightness_up"})
pose, act = settle(ebind, [True, True, True, True, True])
check("FIST entry fires brightness_up", act == "brightness_up")
pose, act = ebind._tick()
check("brightness held inside 250ms no repeat", act is None)
time.sleep(0.26)
pose, act = ebind._tick()
check("brightness held past 250ms repeats", act == "brightness_up")

(gb.read_brightness_pct, gb.set_brightness_pct, gb.refresh_brightness,
 gb.read_volume_pct, gb.set_volume_pct) = _o_br, _o_bw, _o_rf, _o_vr, _o_vs
check("group10 fakes restored",
      gb.read_brightness_pct is _o_br and gb.set_brightness_pct is _o_bw
      and gb.refresh_brightness is _o_rf and gb.read_volume_pct is _o_vr
      and gb.set_volume_pct is _o_vs)

print(f"PASS: {passed} checks")
