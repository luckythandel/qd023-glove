package com.qd023.glove.ble

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.qd023.glove.GloveApplication
import com.qd023.glove.R
import com.qd023.glove.action.ActionDispatcher
import com.qd023.glove.config.ConfigRepository
import com.qd023.glove.config.GloveConfig
import com.qd023.glove.engine.CustomEntry
import com.qd023.glove.engine.GestureEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import timber.log.Timber

class GloveService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private lateinit var ble: BleManager
    private lateinit var engine: GestureEngine
    private lateinit var dispatcher: ActionDispatcher
    private lateinit var repo: ConfigRepository
    private val synth = com.qd023.glove.audio.SynthEngine()
    private var cfg: GloveConfig = GloveConfig.defaults()

    companion object {
        const val CHANNEL_ID = "glove_service"
        const val NOTIF_ID = 1

        /** Sent by the customize screen after saving config.json. */
        const val ACTION_RELOAD = "com.qd023.glove.action.RELOAD_CONFIG"

        @Volatile
        var isRunning: Boolean = false
            private set

        val stateFlow = kotlinx.coroutines.flow.MutableStateFlow<ServiceState>(ServiceState())
    }

    data class ServiceState(
        val connected: Boolean = false,
        val mode: Int = 0,
        val gesture: String? = null,
        val fingers: List<Int> = emptyList(),
        val twist: Int? = null,
        val lastAction: String? = null,
    )

    override fun onCreate() {
        super.onCreate()
        createChannel()

        // Starting a connectedDevice FGS requires BLUETOOTH_* at this exact moment.
        // If the user revoked the permission, fail quietly instead of crashing.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !hasBlePermission()) {
            Timber.e("BLE permission missing - refusing to start foreground service")
            stopSelf()
            return
        }
        if (!startForegroundCompat()) {
            Timber.e("startForeground rejected - stopping service")
            stopSelf()
            return
        }
        isRunning = true

        repo = ConfigRepository(this)
        cfg = repo.load()
        engine = buildEngine(cfg)
        ble = BleManager(this)
        dispatcher = ActionDispatcher(this, getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager, ble)
        // platform hooks
        engine.readVolumePct = { dispatcher.readVolumePercent() }
        engine.readBrightnessPct = { dispatcher.readBrightnessPercent() }
        engine.setVolumePct = { pct -> dispatcher.setVolumeAbsolute(pct); true }
        engine.setBrightnessPct = { pct -> dispatcher.setBrightnessAbsolute(pct); true }
        engine.ledSink = { cmd -> ble.write(cmd) }

        scope.launch {
            ble.connected.collectLatest { c ->
                stateFlow.value = stateFlow.value.copy(connected = c)
                // Never leave notes ringing when the glove drops the link.
                if (!c) synth.stop()
                // BleManager asks the glove for its mode once notifications are up.
            }
        }
        scope.launch {
            ble.lines.collect { line -> handleLine(line) }
        }
        ble.startScan()
    }

    private fun hasBlePermission(): Boolean {
        val scan = ContextCompat.checkSelfPermission(this, android.Manifest.permission.BLUETOOTH_SCAN)
        val conn = ContextCompat.checkSelfPermission(this, android.Manifest.permission.BLUETOOTH_CONNECT)
        return scan == android.content.pm.PackageManager.PERMISSION_GRANTED &&
            conn == android.content.pm.PackageManager.PERMISSION_GRANTED
    }

    private fun buildEngine(c: GloveConfig): GestureEngine {
        val e = GestureEngine(
            bindingsIn = c.bindings,
            thresholdsIn = c.thresholds.mapValues { mapOf("on" to (it.value.on), "off" to (it.value.off)) },
            customIn = c.custom.map { CustomEntry(it.id, it.name, it.pattern, it.action, it.rot) },
        )
        return e
    }

    private fun handleLine(line: String) {
        when {
            line.startsWith("FINGERS:") -> {
                val parts = line.substringAfter("FINGERS:").split(",")
                val pct = parts.mapNotNull { it.trim().toIntOrNull() }
                if (pct.size == 5) {
                    val (disp, action) = engine.onFingers(pct)
                    stateFlow.value = stateFlow.value.copy(fingers = pct, gesture = engine.activeDisplay)
                    if (action != null) {
                        when {
                            engine.ledMode -> {
                                val cmd = LED_ACTION_MAP[action]
                                if (cmd != null) ble.write(cmd + "\n")
                            }
                            engine.androidMode -> {
                                dispatcher.dispatch(action)
                                stateFlow.value = stateFlow.value.copy(lastAction = action)
                            }
                            // WINDOWS mode: this app stays passive so PC actions are not duplicated.
                        }
                    }
                }
            }
            line.startsWith("MUS:") -> {
                val m = line.substringAfter("MUS:").trim().toIntOrNull() ?: return
                if (!engine.musicMode) return
                if (!synth.isRunning) synth.start()
                synth.setFingers(m)
                val notes = (0 until 5)
                    .filter { m and (1 shl it) != 0 }
                    .joinToString(" ") { synth.noteName(it) }
                stateFlow.value = stateFlow.value.copy(
                    lastAction = if (notes.isEmpty()) "—" else notes,
                )
            }
            line.startsWith("TWIST:") -> {
                val d = line.substringAfter("TWIST:").trim().toDoubleOrNull() ?: return
                if (engine.musicMode) synth.setBend(d.toInt())
                val (_, action) = engine.onTwist(d)
                stateFlow.value = stateFlow.value.copy(twist = d.toInt())
                if (action != null && engine.androidMode) {
                    dispatcher.dispatch(action)
                    stateFlow.value = stateFlow.value.copy(lastAction = action)
                }
            }
            line.startsWith("SWEEP:") -> {
                val d = line.substringAfter("SWEEP:").trim().toDoubleOrNull() ?: return
                val (_, action) = engine.onSweep(d)
                if (action != null && engine.androidMode) {
                    dispatcher.dispatch(action)
                    stateFlow.value = stateFlow.value.copy(lastAction = action)
                }
            }
            line.startsWith("MOD:") -> {
                val m = line.substringAfter("MOD:").trim().toIntOrNull() ?: return
                engine.ledMode = (m == 1)
                engine.androidMode = (m == 2)
                engine.windowsMode = (m == 0)
                engine.musicMode = (m == 3)
                if (engine.musicMode) {
                    if (!synth.isRunning) synth.start()
                } else {
                    synth.stop()
                }
                stateFlow.value = stateFlow.value.copy(mode = m)
            }
            line.startsWith("GESTURE:") -> {
                stateFlow.value = stateFlow.value.copy(gesture = line.substringAfter("GESTURE:").trim())
            }
        }
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(CHANNEL_ID, "QD023 Glove", NotificationManager.IMPORTANCE_LOW)
            (getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager).createNotificationChannel(ch)
        }
    }

    private fun startForegroundCompat(): Boolean {
        val notif: Notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("QD023 Glove")
            .setContentText("Listening for gestures")
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .build()
        return try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                startForeground(NOTIF_ID, notif, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            } else {
                startForeground(NOTIF_ID, notif)
            }
            true
        } catch (e: SecurityException) {
            Timber.e(e, "Foreground service start rejected by the system")
            false
        }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_RELOAD) reloadConfig()
        return START_STICKY
    }

    /** Re-read config.json and swap the engine state without dropping the BLE link. */
    private fun reloadConfig() {
        cfg = repo.load()
        engine.resetConfig(
            cfg.bindings,
            cfg.thresholds.mapValues { mapOf("on" to it.value.on, "off" to it.value.off) },
            cfg.custom.map { CustomEntry(it.id, it.name, it.pattern, it.action, it.rot) },
        )
        Timber.i("Config reloaded: %d bindings", cfg.bindings.size)
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        isRunning = false
        scope.cancel()
        synth.stop()
        ble.disconnect()
        super.onDestroy()
    }
}

private val LED_ACTION_MAP = mapOf(
    "play_pause" to "LEDP:T",
    "next" to "LEDM:+1",
    "prev" to "LEDM:-1",
    "volume_up" to "LEDB:+10",
    "volume_down" to "LEDB:-10",
    "brightness_up" to "LEDB:+10",
    "brightness_down" to "LEDB:-10",
    "mute" to "LEDC:cycle",
)
