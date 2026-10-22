package com.qd023.glove.config

import android.content.Context
import org.json.JSONObject
import java.io.File

class ConfigRepository(private val ctx: Context) {
    private val file = File(ctx.filesDir, "config.json")

    fun load(): GloveConfig {
        return try {
            if (!file.exists()) return GloveConfig.defaults()
            val text = file.readText()
            GloveConfig.fromJson(JSONObject(text))
        } catch (e: Exception) {
            GloveConfig.defaults()
        }
    }

    fun save(cfg: GloveConfig) {
        val tmp = File(ctx.filesDir, "config.json.tmp")
        tmp.writeText(cfg.toJson().toString(2))
        tmp.renameTo(file)
    }

    fun exportJson(): String = load().toJson().toString(2)

    fun importJson(text: String): GloveConfig {
        val cfg = GloveConfig.fromJson(JSONObject(text))
        save(cfg)
        return cfg
    }
}
