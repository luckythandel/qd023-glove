package com.qd023.glove.config

import org.json.JSONArray
import org.json.JSONObject

data class Threshold(val on: Int, val off: Int)

data class CustomPattern(
    val id: String,
    val name: String,
    val pattern: List<String>, // "B"/"E"/"X" x5
    val action: String,
    val rot: String = "none", // none/cw/ccw
)

data class GloveConfig(
    val bindings: MutableMap<String, String> = mutableMapOf(),
    val thresholds: MutableMap<String, Threshold> = mutableMapOf(),
    val custom: MutableList<CustomPattern> = mutableListOf(),
) {
    fun toJson(): JSONObject {
        val b = JSONObject()
        bindings.forEach { (k, v) -> b.put(k, v) }
        val t = JSONObject()
        thresholds.forEach { (k, v) -> t.put(k, JSONObject().put("on", v.on).put("off", v.off)) }
        val c = JSONArray()
        custom.forEach { p ->
            c.put(JSONObject()
                .put("id", p.id)
                .put("name", p.name)
                .put("pattern", JSONArray(p.pattern))
                .put("action", p.action)
                .put("rot", p.rot))
        }
        return JSONObject().put("bindings", b).put("thresholds", t).put("custom", c)
    }

    companion object {
        val GESTURE_IDS = listOf(
            "FIST", "INDEX_UP", "PEACE", "THUMBS_UP", "THUMB_BENT",
            "MIDDLE_UP", "PINKY_UP", "OPEN_PALM", "MID_RING", "RING_ONLY",
        )
        val ACTION_IDS = listOf(
            "play_pause", "next", "prev", "volume_up", "volume_down", "mute", "stop",
            "mouse_left", "mouse_right", "mouse_up", "mouse_down",
            "mouse_click", "mouse_rclick", "brightness_up", "brightness_down", "none",
        )
        val FINGER_ORDER = listOf("thumb", "index", "middle", "ring", "pinky")
        val DEFAULT_THRESHOLDS = mapOf(
            "thumb" to Threshold(56, 30),
            "index" to Threshold(46, 18),
            "middle" to Threshold(46, 18),
            "ring" to Threshold(46, 18),
            "pinky" to Threshold(46, 18),
        )
        val DEFAULT_BINDINGS = mapOf(
            "FIST" to "play_pause",
            "INDEX_UP" to "next",
            "PEACE" to "prev",
            "THUMBS_UP" to "none",
            "THUMB_BENT" to "none",
            "MIDDLE_UP" to "volume_down",
            "PINKY_UP" to "mute",
            "OPEN_PALM" to "none",
            "MID_RING" to "none",
            "RING_ONLY" to "none",
        )

        fun fromJson(json: JSONObject): GloveConfig {
            val cfg = GloveConfig()
            val b = json.optJSONObject("bindings")
            if (b != null) {
                val keys = b.keys()
                while (keys.hasNext()) {
                    val k = keys.next()
                    cfg.bindings[k] = b.optString(k, "none")
                }
            }
            if (cfg.bindings.isEmpty()) cfg.bindings.putAll(DEFAULT_BINDINGS)
            val t = json.optJSONObject("thresholds")
            if (t != null) {
                val keys = t.keys()
                while (keys.hasNext()) {
                    val k = keys.next()
                    val o = t.optJSONObject(k) ?: continue
                    cfg.thresholds[k] = Threshold(o.optInt("on", 46), o.optInt("off", 18))
                }
            }
            if (cfg.thresholds.isEmpty()) cfg.thresholds.putAll(DEFAULT_THRESHOLDS)
            val c = json.optJSONArray("custom")
            if (c != null) {
                for (i in 0 until c.length()) {
                    val o = c.optJSONObject(i) ?: continue
                    val pat = o.optJSONArray("pattern") ?: continue
                    val p = mutableListOf<String>()
                    for (j in 0 until pat.length()) p.add(pat.optString(j, "X"))
                    if (p.size != 5) continue
                    cfg.custom.add(CustomPattern(
                        id = o.optString("id", "c$i"),
                        name = o.optString("name", "Custom"),
                        pattern = p,
                        action = o.optString("action", "none"),
                        rot = o.optString("rot", "none"),
                    ))
                }
            }
            return cfg
        }

        fun defaults(): GloveConfig {
            val cfg = GloveConfig()
            cfg.bindings.putAll(DEFAULT_BINDINGS)
            cfg.thresholds.putAll(DEFAULT_THRESHOLDS)
            return cfg
        }
    }
}
