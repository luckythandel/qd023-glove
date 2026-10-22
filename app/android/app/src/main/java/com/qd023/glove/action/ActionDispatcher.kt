package com.qd023.glove.action

import android.content.Context
import android.media.AudioManager
import android.os.Build
import android.provider.Settings
import android.view.KeyEvent
import com.qd023.glove.ble.BleManager

class ActionDispatcher(
    private val ctx: Context,
    private val audio: AudioManager,
    private val ble: BleManager?,
) {
    fun dispatch(action: String) {
        when (action) {
            "volume_up" -> audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_RAISE, AudioManager.FLAG_SHOW_UI)
            "volume_down" -> audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_LOWER, AudioManager.FLAG_SHOW_UI)
            "mute" -> audio.adjustStreamVolume(AudioManager.STREAM_MUSIC, AudioManager.ADJUST_TOGGLE_MUTE, AudioManager.FLAG_SHOW_UI)
            "play_pause" -> mediaKey(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)
            "next" -> mediaKey(KeyEvent.KEYCODE_MEDIA_NEXT)
            "prev" -> mediaKey(KeyEvent.KEYCODE_MEDIA_PREVIOUS)
            "stop" -> mediaKey(KeyEvent.KEYCODE_MEDIA_STOP)
            "brightness_up" -> adjustBrightness(+10)
            "brightness_down" -> adjustBrightness(-10)
            "mouse_left", "mouse_right", "mouse_up", "mouse_down",
            "mouse_click", "mouse_rclick" -> { /* handled by AccessibilityService if enabled */ }
            else -> { /* none or unknown */ }
        }
    }

    fun setVolumeAbsolute(pct: Int) {
        val max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        val target = (pct.coerceIn(0, 100) * max / 100.0).toInt()
        audio.setStreamVolume(AudioManager.STREAM_MUSIC, target, AudioManager.FLAG_SHOW_UI)
    }

    fun setBrightnessAbsolute(pct: Int) {
        if (!Settings.System.canWrite(ctx)) return
        val target = (pct.coerceIn(0, 100) * 255 / 100).coerceIn(0, 255)
        Settings.System.putInt(ctx.contentResolver, Settings.System.SCREEN_BRIGHTNESS, target)
    }

    /** Current media volume as 0..100, or null when unavailable. */
    fun readVolumePercent(): Int? = try {
        val max = audio.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
        if (max <= 0) null
        else (audio.getStreamVolume(AudioManager.STREAM_MUSIC) * 100.0 / max).toInt().coerceIn(0, 100)
    } catch (e: Exception) {
        null
    }

    /** Current screen brightness as 0..100, or null when unavailable. */
    fun readBrightnessPercent(): Int? = try {
        val v = Settings.System.getInt(
            ctx.contentResolver,
            Settings.System.SCREEN_BRIGHTNESS,
            -1,
        )
        if (v < 0) null else (v * 100.0 / 255).toInt().coerceIn(0, 100)
    } catch (e: Exception) {
        null
    }

    private fun mediaKey(code: Int) {
        val ev = KeyEvent(KeyEvent.ACTION_DOWN, code)
        audio.dispatchMediaKeyEvent(ev)
        audio.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, code))
    }

    private fun adjustBrightness(delta: Int) {
        if (!Settings.System.canWrite(ctx)) return
        val cur = Settings.System.getInt(ctx.contentResolver, Settings.System.SCREEN_BRIGHTNESS, 128)
        val target = (cur + delta * 255 / 100).coerceIn(0, 255)
        Settings.System.putInt(ctx.contentResolver, Settings.System.SCREEN_BRIGHTNESS, target)
    }

    fun sendLed(cmd: String) {
        ble?.write(cmd + "\n")
    }
}
