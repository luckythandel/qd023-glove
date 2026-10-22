package com.qd023.glove.engine

import android.os.SystemClock
import java.util.concurrent.ConcurrentHashMap

// ---------------------------------------------------------------------------
// Constants (mirror of glove_bridge.py)
// ---------------------------------------------------------------------------
const val VOLUME_SET_MIN_MS = 60L
const val POSE_STABLE_MS = 150L
const val COOLDOWN_MS = 400L
const val VOLUME_REPEAT_MS = 250L
const val VOLUME_UI_STEP_PCT = 10
const val MOUSE_REPEAT_MS = 80L
const val MOUSE_NUDGE_PX = 60
const val TWIST_STEP_DEG = 10
const val TWIST_FULL_DEG = 180.0
const val SWEEP_FULL_DEG = 90.0
const val TWIST_RATE_DT_MAX_S = 1.5
const val TWIST_PAUSE_DPS_DEFAULT = 58.0
const val TWIST_RESUME_DPS_DEFAULT = 60.0
const val SWEEP_PAUSE_DPS_DEFAULT = 12.0
const val SWEEP_RESUME_DPS_DEFAULT = 20.0
const val ROT_TRIGGER_DEG = 45.0
const val TWIST_PAUSE_SAMPLES = 2
const val TWIST_RESUME_SAMPLES = 2

typealias Pct = List<Int>

data class CustomEntry(
    val id: String,
    val name: String,
    val pattern: List<String>,
    val action: String,
    val rot: String = "none",
)

class GestureEngine(
    bindingsIn: Map<String, String>,
    thresholdsIn: Map<String, Map<String, Int>>? = null,
    customIn: List<CustomEntry>? = null,
) {
    // ---- config ----
    var bindings: MutableMap<String, String> = bindingsIn.toMutableMap()
    var thresholds: MutableMap<String, MutableMap<String, Int>> =
        (thresholdsIn ?: defaultThresholds()).mapValues { it.value.toMutableMap() }.toMutableMap()
    var custom: MutableList<CustomEntry> = (customIn ?: emptyList()).toMutableList()

    // ---- state ----
    val state: MutableList<Boolean?> = mutableListOf(null, null, null, null, null)
    private var _candidate: String? = null
    private var _candidateSince: Long = 0L
    var activePose: String? = null
    var activeDisplay: String? = null
    private val _lastFire: MutableMap<String, Long> = mutableMapOf()

    // twist state
    private var _twistBaseline: Double? = null
    private var _twistV0: Int? = null
    private var _twistLastSet: Int? = null
    private var _twistLastSetTs: Long = 0L
    private var _twistLastUi: Int? = null
    private var _twistMotion: String = "frozen"
    var twistPauseDps: Double = TWIST_PAUSE_DPS_DEFAULT
    var twistResumeDps: Double = TWIST_RESUME_DPS_DEFAULT
    private var _twistSlowN: Int = 0
    private var _twistFastN: Int = 0
    private var _twistPrevDeg: Double? = null
    private var _twistPrevTs: Long? = null
    private var _twistBackend: String? = null
    private var _sweepInRing: Boolean = false
    private var _rotDeg: Double? = null
    private var _rotRefDeg: Double? = null
    private var _rotRefId: String? = null

    // mode flags
    var ledMode: Boolean = false
    var androidMode: Boolean = false
    var windowsMode: Boolean = true
    var musicMode: Boolean = false

    // platform hooks (set by the host)
    var readVolumePct: (() -> Int?)? = null
    var readBrightnessPct: (() -> Int?)? = null
    var setVolumePct: ((Int) -> Boolean)? = null
    var setBrightnessPct: ((Int) -> Boolean)? = null
    var toggleMute: (() -> Boolean?)? = null
    var refreshBrightness: (() -> Unit)? = null
    var ledSink: ((String) -> Unit)? = null

    // ---- config setters (use the property setters directly) ----
    fun resetConfig(
        b: Map<String, String>,
        t: Map<String, Map<String, Int>>,
        c: List<CustomEntry>,
    ) {
        bindings = b.toMutableMap()
        thresholds = t.mapValues { it.value.toMutableMap() }.toMutableMap()
        custom = c.toMutableList()
        _rotRefId = null
        _rotRefDeg = null
    }

    fun display_name(key: String?): String? {
        if (key == null) return null
        if (key.startsWith("custom:")) {
            val cid = key.substringAfter("custom:")
            for (e in custom) if (e.id == cid) return e.name
            return "Custom"
        }
        return key
    }

    fun _action_for(key: String?): String? {
        if (key == null) return null
        if (key.startsWith("custom:")) {
            val cid = key.substringAfter("custom:")
            for (e in custom) if (e.id == cid) return e.action
            return null
        }
        return bindings[key] ?: "none"
    }

    fun bentFlags(): List<Boolean> = state.map { it == true }

    private fun _reconcile(idx: Int, p: Int) {
        val name = FINGER_ORDER[idx]
        val th = thresholds[name] ?: defaultThresholds()[name]!!
        val on = th["on"] ?: 46
        val off = th["off"] ?: 18
        val cur = state[idx]
        if (cur == null) {
            state[idx] = p >= on
            return
        }
        if (p >= on) state[idx] = true
        else if (p <= off) state[idx] = false
    }

    fun onFingers(pct: List<Int>): Pair<String?, String?> {
        for (idx in 0 until minOf(5, pct.size)) _reconcile(idx, pct[idx])
        return _tick()
    }

    fun onFingerEdge(name: String, bent: Boolean): Pair<String?, String?> {
        val idx = FINGER_NAME_TO_IDX[name.trim().lowercase()] ?: return Pair(null, null)
        if (idx == 0) return Pair(null, null) // thumb handled via pct reconcile
        state[idx] = bent
        return _tick()
    }

    private fun _twistReset() {
        _twistBaseline = null
        _twistV0 = null
        _twistLastSet = null
        _twistLastSetTs = 0L
        _twistLastUi = null
        _twistMotion = "frozen"
        _twistSlowN = 0
        _twistFastN = 0
        _twistPrevDeg = null
        _twistPrevTs = null
        _twistBackend = null
        _sweepInRing = false
    }

    fun onTwist(deg: Double): Pair<String?, String?> {
        _rotDeg = deg
        if (androidMode) return Pair(null, null)
        if (activePose == "RING_ONLY") return Pair(null, null)
        if (activePose != "MID_RING") {
            _twistReset()
            return Pair(null, null)
        }
        return _absFsm(deg, "volume", twistPauseDps, twistResumeDps)
    }

    fun onSweep(deg: Double): Pair<String?, String?> {
        if (androidMode) return Pair(null, null)
        if (activePose != "RING_ONLY") {
            _sweepInRing = false
            return Pair(null, null)
        }
        if (!_sweepInRing) _twistBaseline = null
        _sweepInRing = true
        return _absFsm(deg, "brightness", SWEEP_PAUSE_DPS_DEFAULT, SWEEP_RESUME_DPS_DEFAULT)
    }

    private fun _absFsm(deg: Double, backend: String, pauseDps: Double, resumeDps: Double): Pair<String?, String?> {
        val now = nowMs()
        var rate: Double? = null
        val prevTs = _twistPrevTs
        val prevDeg = _twistPrevDeg
        if (prevTs != null && prevDeg != null) {
            val dt = (now - prevTs) / 1000.0
            rate = if (dt <= 0.0 || dt > TWIST_RATE_DT_MAX_S) 0.0
                   else (deg - prevDeg) / dt
        }
        _twistPrevDeg = deg
        _twistPrevTs = now

        if (activePose !in listOf("MID_RING", "RING_ONLY")) {
            _twistReset()
            return Pair(null, null)
        }
        if (_twistBackend != null && _twistBackend != backend) {
            _twistReset()
        }
        _twistBackend = backend

        if (rate != null) {
            val a = kotlin.math.abs(rate)
            if (a <= pauseDps) {
                _twistSlowN += 1
                _twistFastN = 0
            } else if (a >= resumeDps) {
                _twistFastN += 1
                _twistSlowN = 0
            } else {
                return Pair(null, null)
            }
        }

        if (_twistBaseline == null) {
            val v: Int = if (ledMode) {
                50
            } else if (backend == "brightness") {
                val rb = readBrightnessPct?.invoke()
                if (rb == null) {
                    refreshBrightness?.invoke()
                    return Pair(null, null)
                }
                rb
            } else {
                readVolumePct?.invoke() ?: 0
            }
            _twistBaseline = deg
            _twistV0 = v
            _twistLastUi = v
            _twistLastSet = null
            _twistMotion = "frozen"
            _twistSlowN = 0
            _twistFastN = 0
            return Pair(null, null)
        }

        if (_twistMotion == "frozen") {
            if (_twistFastN >= TWIST_RESUME_SAMPLES) {
                val v: Int = if (ledMode) {
                    50
                } else if (backend == "brightness") {
                    val rb = readBrightnessPct?.invoke()
                    if (rb == null) {
                        refreshBrightness?.invoke()
                        _twistBaseline = deg
                        return Pair(null, null)
                    }
                    rb
                } else {
                    readVolumePct?.invoke() ?: (_twistLastSet ?: 0)
                }
                _twistMotion = "active"
                _twistBaseline = deg
                _twistV0 = v
                _twistLastUi = v
                _twistLastSet = v
                _twistSlowN = 0
                _twistFastN = 0
            } else {
                _twistBaseline = deg
            }
            return Pair(null, null)
        }

        if (_twistSlowN >= TWIST_PAUSE_SAMPLES) {
            _twistMotion = "frozen"
            _twistBaseline = deg
            _twistSlowN = 0
            return Pair(null, null)
        }
        if (rate == null || kotlin.math.abs(rate) <= pauseDps) {
            return Pair(null, null)
        }

        val delta = deg - (_twistBaseline ?: deg)

        if (_twistV0 == null) {
            if (ledMode) {
                _twistV0 = 50
            } else if (backend == "brightness") {
                refreshBrightness?.invoke()
                return Pair(null, null)
            } else {
                // pycaw unavailable -> relative media-key steps
                val action = if (delta >= TWIST_STEP_DEG) "volume_up"
                             else if (delta <= -TWIST_STEP_DEG) "volume_down"
                             else null
                if (action != null) {
                    val last = _lastFire[action] ?: 0L
                    if ((now - last) >= VOLUME_REPEAT_MS) {
                        _lastFire[action] = now
                        _twistBaseline = _twistBaseline!! + if (action == "volume_up") TWIST_STEP_DEG.toDouble() else -TWIST_STEP_DEG.toDouble()
                        return Pair(null, action)
                    }
                }
                return Pair(null, null)
            }
        }

        val fullDeg = if (backend == "brightness") SWEEP_FULL_DEG else TWIST_FULL_DEG
        var target = ((_twistV0 ?: 50) + (delta * (100.0 / fullDeg))).toInt()
        target = target.coerceIn(0, 100)
        if (target == _twistLastSet) return Pair(null, null)
        if ((now - _twistLastSetTs) < VOLUME_SET_MIN_MS.toInt()) return Pair(null, null)

        if (ledMode && ledSink != null) {
            if (backend == "brightness") ledSink!!("LEDH:%d".format((target * 3.6).toInt()))
            else ledSink!!("LEDB:%d".format(target))
        } else if (backend == "brightness") {
            val ok = setBrightnessPct?.invoke(target) ?: false
            if (!ok) return Pair(null, null)
        } else {
            val ok = setVolumePct?.invoke(target) ?: false
            if (!ok) return Pair(null, null)
        }
        _twistLastSet = target
        _twistLastSetTs = now

        val flash: String? = if (_twistLastUi == null || kotlin.math.abs(target - (_twistLastUi ?: 0)) >= VOLUME_UI_STEP_PCT) {
            val up = _twistLastUi == null || target > (_twistLastUi ?: 0)
            if (backend == "brightness" || ledMode) {
                if (up) "brightness_up" else "brightness_down"
            } else {
                if (up) "volume_up" else "volume_down"
            }
        } else null
        _twistLastUi = target

        return Pair(flash, null)
    }

    private fun _tick(): Pair<String?, String?> {
        if (state.any { it == null }) return Pair(null, null)
        val bent = state.map { it == true }
        val raw = classifyPose(bent) ?: run {
            val entry = _rotGate(matchCustom(bent, custom))
            if (entry != null) "custom:${entry.id}" else null
        }
        // built-in pose matched: clear latched rot baseline
        if (raw != null && !raw.startsWith("custom:")) {
            _rotRefId = null
            _rotRefDeg = null
        }

        val now = nowMs()
        if (raw != _candidate) {
            _candidate = raw
            _candidateSince = now
        }

        var poseChanged = false
        if (_candidate != null && activePose != _candidate &&
            (now - _candidateSince) >= POSE_STABLE_MS) {
            activePose = _candidate
            activeDisplay = display_name(_candidate)
            poseChanged = true
        } else if (_candidate == null && activePose != null &&
            (now - _candidateSince) >= POSE_STABLE_MS) {
            activePose = null
            activeDisplay = null
            poseChanged = true
        }

        if (poseChanged && activePose !in listOf("MID_RING", "RING_ONLY")) {
            _twistReset()
        }

        var action: String? = null
        val act = _action_for(activePose)
        if (act != null && act != "none") {
            val elapsed = now - (_lastFire[act] ?: 0L)
            if (act in MOUSE_NUDGE_ACTIONS) {
                if (elapsed >= MOUSE_REPEAT_MS) {
                    action = act
                    _lastFire[act] = now
                }
            } else if (act in VOLUME_ACTIONS || act in BRIGHTNESS_ACTIONS) {
                if (elapsed >= VOLUME_REPEAT_MS) {
                    action = act
                    _lastFire[act] = now
                }
            } else if (poseChanged) {
                if (elapsed >= COOLDOWN_MS) {
                    action = act
                    _lastFire[act] = now
                }
            }
        }

        val display = if (poseChanged) (activeDisplay ?: activePose) else null
        return Pair(display, action)
    }

    // ---- rot gate (custom patterns) ----
    private fun _rotGate(entry: CustomEntry?): CustomEntry? {
        if (entry == null) return null
        val rot = entry.rot
        if (rot == "none" || rot.isBlank()) return entry
        val deg = _rotDeg ?: return null
        if (_rotRefId != entry.id) {
            _rotRefId = entry.id
            _rotRefDeg = deg
            return null // first sight: latch baseline, don't fire
        }
        val ref = _rotRefDeg ?: return null
        val delta = deg - ref
        return if (rot == "cw" && delta >= ROT_TRIGGER_DEG) entry
               else if (rot == "ccw" && delta <= -ROT_TRIGGER_DEG) entry
               else null
    }

    private fun matchCustom(bent: List<Boolean>, customs: List<CustomEntry>): CustomEntry? {
        for (e in customs) {
            val pat = e.pattern
            if (pat.size != 5) continue
            var hit = true
            for (i in 0..4) {
                val want = pat[i]
                if (want == "X") continue
                if ((want == "B") != bent[i]) { hit = false; break }
            }
            if (hit) return e
        }
        return null
    }

    private fun nowMs(): Long = SystemClock.elapsedRealtime()

    companion object {
        val FINGER_ORDER = listOf("thumb", "index", "middle", "ring", "pinky")
        val FINGER_NAME_TO_IDX = mapOf(
            "thumb" to 0, "index" to 1, "middle" to 2, "ring" to 3, "pinky" to 4
        )
        val MOUSE_NUDGE_ACTIONS = setOf("mouse_left", "mouse_right", "mouse_up", "mouse_down")
        val VOLUME_ACTIONS = setOf("volume_up", "volume_down")
        val BRIGHTNESS_ACTIONS = setOf("brightness_up", "brightness_down")

        fun defaultThresholds(): Map<String, Map<String, Int>> = mapOf(
            "thumb" to mapOf("on" to 56, "off" to 30),
            "index" to mapOf("on" to 46, "off" to 18),
            "middle" to mapOf("on" to 46, "off" to 18),
            "ring" to mapOf("on" to 46, "off" to 18),
            "pinky" to mapOf("on" to 46, "off" to 18),
        )

        fun classifyPose(bent: List<Boolean>): String? {
            val t = bent[0]; val i = bent[1]; val m = bent[2]; val r = bent[3]; val p = bent[4]
            val bentA = bent.count { it }
            val nActive = bent.size // assume all active (5)
            if (bentA == 0) return "OPEN_PALM"
            if (bentA == bent.size) return "FIST"
            if (t && !i && !m && !r && !p) return "THUMB_BENT"
            if (!t && i && m && r && p) return "THUMBS_UP"
            if (!i && m && r && p) return "INDEX_UP"
            if (!i && !m && r && p) return "PEACE"
            if (!m && i && r && p) return "MIDDLE_UP"
            if (i && m && r && !p) return "PINKY_UP"
            if (!i && m && r && !p) return "MID_RING"
            if (!i && !m && r && !p) return "RING_ONLY"
            return null
        }

        fun matchCustom(bent: List<Boolean>, custom: List<CustomEntry>): CustomEntry? {
            for (e in custom) {
                val pat = e.pattern
                if (pat.size != 5) continue
                var hit = true
                for (idx in 0..4) {
                    val want = pat[idx]
                    if (want == "X") continue
                    if ((want == "B") != bent[idx]) { hit = false; break }
                }
                if (hit) return e
            }
            return null
        }
    }
}
