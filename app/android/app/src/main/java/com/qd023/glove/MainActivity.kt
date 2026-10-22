package com.qd023.glove

import android.Manifest
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.view.WindowCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.qd023.glove.ble.GloveService
import com.qd023.glove.config.ConfigRepository
import com.qd023.glove.config.GloveConfig
import com.qd023.glove.ui.theme.BgDeep
import com.qd023.glove.ui.theme.FingerGauge
import com.qd023.glove.ui.theme.GloveTheme
import com.qd023.glove.ui.theme.NeonAmber
import com.qd023.glove.ui.theme.NeonButton
import com.qd023.glove.ui.theme.NeonCyan
import com.qd023.glove.ui.theme.NeonGreen
import com.qd023.glove.ui.theme.NeonMagenta
import com.qd023.glove.ui.theme.NeonMiniButton
import com.qd023.glove.ui.theme.NeonPanel
import com.qd023.glove.ui.theme.NeonStat
import com.qd023.glove.ui.theme.NeonText
import com.qd023.glove.ui.theme.NeonPurple
import com.qd023.glove.ui.theme.NeonRed
import com.qd023.glove.ui.theme.ReadoutStyle
import com.qd023.glove.ui.theme.StatusChip
import com.qd023.glove.ui.theme.TextBright
import com.qd023.glove.ui.theme.TextBody
import com.qd023.glove.ui.theme.TextDim
import com.qd023.glove.ui.theme.scanlines

/** Runtime permissions the app actually needs, split by purpose. */
object GlovePermissions {
    /** Permissions required before the BLE foreground service may start. */
    fun ble(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    fun notifications(): Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            arrayOf(Manifest.permission.POST_NOTIFICATIONS)
        } else {
            emptyArray()
        }

    fun all(): Array<String> = ble() + notifications()

    fun granted(ctx: Context, permission: String): Boolean =
        ContextCompat.checkSelfPermission(ctx, permission) == PackageManager.PERMISSION_GRANTED

    fun hasBle(ctx: Context): Boolean = ble().all { granted(ctx, it) }

    fun hasNotifications(ctx: Context): Boolean = notifications().all { granted(ctx, it) }

    fun canWriteSettings(ctx: Context): Boolean = Settings.System.canWrite(ctx)

    fun hasAccessibility(ctx: Context): Boolean {
        val enabled = Settings.Secure.getString(
            ctx.contentResolver,
            Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES,
        ) ?: return false
        return enabled.split(':').any {
            it.contains(ctx.packageName) && it.contains("GloveAccessibilityService")
        }
    }
}

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        WindowCompat.getInsetsController(window, window.decorView).apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
        }
        setContent {
            GloveTheme {
                Surface(color = BgDeep) {
                    GloveApp(this)
                }
            }
        }
    }
}

@Composable
fun GloveApp(activity: ComponentActivity) {
    var showCustomize by remember { mutableStateOf(false) }
    if (showCustomize) {
        BindingsScreen(activity) { showCustomize = false }
    } else {
        GloveScreen(activity) { showCustomize = true }
    }
}

@Composable
fun GloveScreen(activity: ComponentActivity, onCustomize: () -> Unit) {
    val ctx = LocalContext.current
    val state by GloveService.stateFlow.collectAsState()

    var serviceRunning by remember { mutableStateOf(GloveService.isRunning) }
    var tick by remember { mutableIntStateOf(0) }
    var message by remember { mutableStateOf<String?>(null) }
    var pendingStart by remember { mutableStateOf(false) }

    val lifecycleOwner = LocalLifecycleOwner.current
    remember(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) {
                tick++
                serviceRunning = GloveService.isRunning
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        observer
    }

    fun startService() {
        if (!GlovePermissions.hasBle(ctx)) {
            pendingStart = true
            message = "Bluetooth permission required before the service can start."
            return
        }
        ContextCompat.startForegroundService(ctx, Intent(ctx, GloveService::class.java))
        serviceRunning = true
        message = null
    }

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        tick++
        if (GlovePermissions.hasBle(ctx)) {
            if (pendingStart) {
                pendingStart = false
                startService()
            } else {
                message = null
            }
        } else {
            pendingStart = false
            message = "Bluetooth permission denied. Enable it in Settings > Apps > QD023 Glove."
        }
    }

    val hasBle = remember(tick) { GlovePermissions.hasBle(ctx) }
    val hasNotif = remember(tick) { GlovePermissions.hasNotifications(ctx) }
    val canWrite = remember(tick) { GlovePermissions.canWriteSettings(ctx) }
    val hasA11y = remember(tick) { GlovePermissions.hasAccessibility(ctx) }

    val modeAccent = when (state.mode) {
        2 -> NeonMagenta
        1 -> NeonAmber
        else -> NeonCyan
    }

    Box(
        Modifier
            .fillMaxSize()
            .background(
                Brush.radialGradient(
                    colors = listOf(NeonPurple.copy(alpha = 0.20f), BgDeep),
                    center = Offset(180f, 0f),
                    radius = 1100f,
                )
            )
            .scanlines(0.02f),
    ) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .statusBarsPadding()
                .navigationBarsPadding()
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            // ---- header ----
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Row {
                        NeonText(
                            text = "QD023",
                            color = NeonCyan,
                            style = MaterialTheme.typography.headlineMedium,
                            blur = 18f,
                        )
                        NeonText(
                            text = " GLOVE",
                            color = NeonMagenta,
                            style = MaterialTheme.typography.headlineMedium,
                            blur = 18f,
                        )
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        text = "GESTURE CONTROLLER // ANDROID",
                        style = MaterialTheme.typography.labelSmall,
                        color = TextDim,
                    )
                }
                Column(horizontalAlignment = Alignment.End) {
                    StatusChip(state.connected)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        text = if (serviceRunning) "LIVE" else "IDLE",
                        style = MaterialTheme.typography.labelSmall,
                        color = if (serviceRunning) NeonGreen else TextDim,
                    )
                }
            }

            // ---- status ----
            NeonPanel(title = "Link Status", accent = modeAccent) {
                NeonStat(
                    label = "Link",
                    value = if (state.connected) "CONNECTED" else "SEARCHING",
                    accent = if (state.connected) NeonGreen else NeonRed,
                )
                NeonStat(
                    label = "Mode",
                    value = modeName(state.mode),
                    accent = modeAccent,
                )
                NeonStat(
                    label = "Gesture",
                    value = state.gesture ?: "—",
                    accent = TextBright,
                )
                NeonStat(
                    label = "Action",
                    value = state.lastAction ?: "—",
                    accent = NeonCyan,
                )
                NeonStat(
                    label = "Twist",
                    value = state.twist?.let { "$it°" } ?: "—",
                    accent = TextBright,
                )

                Spacer(Modifier.height(10.dp))
                Text(
                    text = "FINGER CHANNELS",
                    style = MaterialTheme.typography.labelSmall,
                    color = TextDim,
                )
                Spacer(Modifier.height(6.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    val names = listOf("T", "I", "M", "R", "P")
                    for (i in 0 until 5) {
                        FingerGauge(
                            label = names[i],
                            value = state.fingers.getOrElse(i) { 0 },
                            accent = modeAccent,
                            modifier = Modifier.weight(1f),
                        )
                    }
                }
            }

            // ---- controls ----
            NeonPanel(title = "Control", accent = NeonGreen) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                ) {
                    NeonButton(
                        text = if (serviceRunning) "Restart" else "Start",
                        accent = NeonGreen,
                        onClick = {
                            if (serviceRunning) {
                                // A bare startForegroundService() on a live service
                                // never re-runs onCreate, so the BLE scan stays stale.
                                ctx.stopService(Intent(ctx, GloveService::class.java))
                                serviceRunning = false
                                android.os.Handler(android.os.Looper.getMainLooper())
                                    .postDelayed({ startService() }, 500)
                            } else {
                                startService()
                            }
                        },
                        modifier = Modifier.weight(1f),
                    )
                    NeonButton(
                        text = "Stop",
                        accent = NeonRed,
                        onClick = {
                            ctx.stopService(Intent(ctx, GloveService::class.java))
                            serviceRunning = false
                            message = "Service stopped."
                        },
                        modifier = Modifier.weight(1f),
                    )
                }
                Spacer(Modifier.height(10.dp))
                NeonButton(
                    text = "Customize Bindings",
                    accent = NeonPurple,
                    onClick = onCustomize,
                    modifier = Modifier.fillMaxWidth(),
                    height = 44.dp,
                )
            }

            // ---- permissions ----
            NeonPanel(title = "Permissions", accent = NeonCyan) {
                PermissionRow(
                    label = "BLUETOOTH",
                    ok = hasBle,
                    action = "Grant",
                    accent = NeonCyan,
                    onAction = { permissionLauncher.launch(GlovePermissions.ble()) },
                )
                PermissionRow(
                    label = "NOTIFICATIONS",
                    ok = hasNotif,
                    action = "Grant",
                    accent = NeonCyan,
                    onAction = { permissionLauncher.launch(GlovePermissions.notifications()) },
                )
                PermissionRow(
                    label = "SYSTEM SETTINGS",
                    ok = canWrite,
                    action = "Open",
                    accent = NeonAmber,
                    onAction = {
                        ctx.startActivity(
                            Intent(
                                Settings.ACTION_MANAGE_WRITE_SETTINGS,
                                Uri.parse("package:${ctx.packageName}"),
                            )
                        )
                    },
                )
                PermissionRow(
                    label = "ACCESSIBILITY",
                    ok = hasA11y,
                    action = "Open",
                    accent = NeonAmber,
                    onAction = {
                        ctx.startActivity(Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS))
                    },
                )
            }

            // ---- live gesture map ----
            val cfg = remember(tick) {
                runCatching { ConfigRepository(ctx).load() }.getOrNull()
            }
            NeonPanel(title = "Gesture Map", accent = NeonPurple) {
                GloveConfig.GESTURE_IDS.forEach { g ->
                    val action = cfg?.bindings?.get(g) ?: return@forEach
                    if (action != "none") {
                        GestureRow(prettyGesture(g), prettyAction(action))
                    }
                }
                GestureRow("TWIST", "VOLUME (ABS)")
                GestureRow("SWEEP", "BRIGHTNESS")
            }

            if (message != null) {
                Text(
                    text = message!!,
                    style = MaterialTheme.typography.bodySmall,
                    color = NeonRed,
                )
            }

            // ---- footer ----
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    text = "K1 CYCLES  ",
                    style = MaterialTheme.typography.labelSmall,
                    color = TextDim,
                )
                Text(
                    text = "WINDOWS > LED > ANDROID",
                    style = ReadoutStyle,
                    color = TextDim,
                )
            }
            Text(
                text = "Actions fire only while the glove is in ANDROID mode.",
                style = MaterialTheme.typography.bodySmall,
                color = TextDim,
            )
            Spacer(Modifier.height(8.dp))
        }
    }
}

@Composable
private fun PermissionRow(
    label: String,
    ok: Boolean,
    action: String,
    accent: Color,
    onAction: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            color = TextDim,
            modifier = Modifier.weight(1f),
        )
        StatusChip(ok)
        NeonMiniButton(
            text = action,
            accent = accent,
            onClick = onAction,
            enabled = !ok,
            modifier = Modifier.padding(start = 8.dp),
        )
    }
}

@Composable
private fun GestureRow(gesture: String, action: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = gesture,
            style = MaterialTheme.typography.labelSmall,
            color = TextBody,
            modifier = Modifier.weight(1f),
        )
        Text(
            text = action,
            style = ReadoutStyle,
            color = NeonPurple,
            maxLines = 1,
        )
    }
}

private fun modeName(m: Int): String = when (m) {
    0 -> "WINDOWS"
    1 -> "LED STRIP"
    2 -> "ANDROID"
    3 -> "MUSIC"
    else -> "UNKNOWN"
}
