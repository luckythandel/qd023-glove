package com.qd023.glove

import android.content.Intent
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.qd023.glove.ble.GloveService
import com.qd023.glove.config.ConfigRepository
import com.qd023.glove.config.GloveConfig
import com.qd023.glove.config.Threshold
import com.qd023.glove.ui.theme.BgDeep
import com.qd023.glove.ui.theme.NeonAmber
import com.qd023.glove.ui.theme.NeonButton
import com.qd023.glove.ui.theme.NeonCyan
import com.qd023.glove.ui.theme.NeonGreen
import com.qd023.glove.ui.theme.NeonMagenta
import com.qd023.glove.ui.theme.NeonPanel
import com.qd023.glove.ui.theme.NeonPurple
import com.qd023.glove.ui.theme.NeonRed
import com.qd023.glove.ui.theme.NeonSquare
import com.qd023.glove.ui.theme.NeonText
import com.qd023.glove.ui.theme.ReadoutStyle
import com.qd023.glove.ui.theme.TextBody
import com.qd023.glove.ui.theme.TextBright
import com.qd023.glove.ui.theme.TextDim
import com.qd023.glove.ui.theme.neonFrame
import com.qd023.glove.ui.theme.scanlines

/** Actions that are actually wired on Android today. */
private val PICKER_ACTIONS = listOf(
    "play_pause",
    "next",
    "prev",
    "stop",
    "volume_up",
    "volume_down",
    "mute",
    "brightness_up",
    "brightness_down",
    "none",
)

@Composable
fun BindingsScreen(activity: ComponentActivity, onBack: () -> Unit) {
    val ctx = LocalContext.current
    BackHandler(onBack = onBack)

    val repo = remember { ConfigRepository(ctx) }
    val original = remember { repo.load() }

    val bindings = remember {
        androidx.compose.runtime.mutableStateMapOf<String, String>().apply {
            putAll(original.bindings)
        }
    }
    val thresholds = remember {
        androidx.compose.runtime.mutableStateMapOf<String, Threshold>().apply {
            putAll(original.thresholds)
        }
    }

    var tab by remember { mutableStateOf(0) }
    var editing by remember { mutableStateOf<String?>(null) }
    var saved by remember { mutableStateOf(false) }

    fun persist() {
        val cfg = GloveConfig(
            bindings = bindings.toMutableMap(),
            thresholds = thresholds.toMutableMap(),
            custom = original.custom.toMutableList(),
        )
        repo.save(cfg)
        if (GloveService.isRunning) {
            ctx.startService(
                Intent(ctx, GloveService::class.java).setAction(GloveService.ACTION_RELOAD)
            )
        }
        saved = true
    }

    Box(
        Modifier
            .fillMaxSize()
            .background(
                Brush.radialGradient(
                    colors = listOf(NeonMagenta.copy(alpha = 0.14f), BgDeep),
                    center = Offset(160f, 0f),
                    radius = 1000f,
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
            Row(verticalAlignment = Alignment.CenterVertically) {
                NeonSquare(text = "<", accent = NeonCyan, onClick = onBack)
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    NeonText(
                        text = "CUSTOMIZE",
                        color = NeonCyan,
                        style = MaterialTheme.typography.headlineMedium,
                        blur = 16f,
                    )
                    Text(
                        text = "MAP GESTURES TO ANDROID ACTIONS",
                        style = MaterialTheme.typography.labelSmall,
                        color = TextDim,
                    )
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                TabButton(
                    text = "BINDINGS",
                    selected = tab == 0,
                    accent = NeonCyan,
                    modifier = Modifier.weight(1f),
                ) { tab = 0 }
                TabButton(
                    text = "THRESHOLDS",
                    selected = tab == 1,
                    accent = NeonAmber,
                    modifier = Modifier.weight(1f),
                ) { tab = 1 }
            }

            if (tab == 0) {
                NeonPanel(title = "Gesture Bindings", accent = NeonCyan) {
                    GloveConfig.GESTURE_IDS.forEach { g ->
                        BindingRow(
                            gesture = prettyGesture(g),
                            action = prettyAction(bindings[g] ?: "none"),
                            accent = NeonCyan,
                            onClick = { editing = g },
                        )
                    }
                    Spacer(Modifier.height(6.dp))
                    Text(
                        text = "Tap a gesture to choose its action. Unused gestures are set to NONE.",
                        style = MaterialTheme.typography.bodySmall,
                        color = TextDim,
                    )
                }
            } else {
                NeonPanel(title = "Finger Thresholds", accent = NeonAmber) {
                    Text(
                        text = "Bend percentage that marks a finger closed (ON) and open (OFF).",
                        style = MaterialTheme.typography.bodySmall,
                        color = TextDim,
                    )
                    Spacer(Modifier.height(10.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Spacer(Modifier.weight(1f))
                        Text(
                            text = "ON",
                            style = MaterialTheme.typography.labelSmall,
                            color = TextDim,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.width(96.dp),
                        )
                        Spacer(Modifier.width(10.dp))
                        Text(
                            text = "OFF",
                            style = MaterialTheme.typography.labelSmall,
                            color = TextDim,
                            textAlign = TextAlign.Center,
                            modifier = Modifier.width(96.dp),
                        )
                    }
                    GloveConfig.FINGER_ORDER.forEach { f ->
                        val t = thresholds[f] ?: Threshold(46, 18)
                        ThresholdRow(
                            label = f.uppercase(),
                            threshold = t,
                            onChange = { thresholds[f] = it },
                        )
                    }
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                NeonButton(
                    text = "Save",
                    accent = NeonGreen,
                    onClick = { persist() },
                    modifier = Modifier.weight(1f),
                )
                NeonButton(
                    text = "Defaults",
                    accent = NeonAmber,
                    onClick = {
                        bindings.clear()
                        bindings.putAll(GloveConfig.DEFAULT_BINDINGS)
                        thresholds.clear()
                        thresholds.putAll(GloveConfig.DEFAULT_THRESHOLDS)
                        saved = false
                    },
                    modifier = Modifier.weight(1f),
                )
            }

            Text(
                text = if (saved) {
                    "Saved. Applied to the running service immediately."
                } else {
                    "Changes take effect after Save."
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (saved) NeonGreen else TextDim,
            )
            Spacer(Modifier.height(8.dp))
        }
    }

    val target = editing
    if (target != null) {
        ActionPickerDialog(
            gesture = target,
            current = bindings[target] ?: "none",
            onPick = { action ->
                bindings[target] = action
                editing = null
            },
            onDismiss = { editing = null },
        )
    }
}

@Composable
private fun TabButton(
    text: String,
    selected: Boolean,
    accent: Color,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val shape = RoundedCornerShape(4.dp)
    Box(
        modifier = modifier
            .height(40.dp)
            .clip(shape)
            .background(accent.copy(alpha = if (selected) 0.18f else 0.04f))
            .border(1.dp, accent.copy(alpha = if (selected) 0.8f else 0.22f), shape)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelLarge,
            color = if (selected) TextBright else TextDim,
        )
    }
}

@Composable
private fun BindingRow(
    gesture: String,
    action: String,
    accent: Color,
    onClick: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(3.dp))
            .clickable(onClick = onClick)
            .padding(vertical = 8.dp, horizontal = 2.dp),
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
            color = if (action == "NONE") TextDim else accent,
            maxLines = 1,
        )
        Spacer(Modifier.width(6.dp))
        Text(">", style = MaterialTheme.typography.labelSmall, color = TextDim)
    }
}

@Composable
private fun ThresholdRow(
    label: String,
    threshold: Threshold,
    onChange: (Threshold) -> Unit,
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
            color = TextBody,
            modifier = Modifier.weight(1f),
        )
        Stepper(
            value = threshold.on,
            accent = NeonAmber,
            onChange = { v ->
                val on = v.coerceIn(1, 100)
                onChange(Threshold(on, threshold.off.coerceAtMost(on - 1)))
            },
        )
        Spacer(Modifier.width(10.dp))
        Stepper(
            value = threshold.off,
            accent = NeonCyan,
            onChange = { v ->
                val off = v.coerceIn(0, 99)
                onChange(Threshold(threshold.on.coerceAtLeast(off + 1), off))
            },
        )
    }
}

@Composable
private fun Stepper(value: Int, accent: Color, onChange: (Int) -> Unit) {
    Row(
        modifier = Modifier.width(96.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        NeonSquare(text = "-", accent = accent, onClick = { onChange(value - 2) }, size = 26.dp)
        Text(
            text = "$value",
            style = ReadoutStyle,
            color = accent,
            textAlign = TextAlign.Center,
            modifier = Modifier.width(38.dp),
        )
        NeonSquare(text = "+", accent = accent, onClick = { onChange(value + 2) }, size = 26.dp)
    }
}

@Composable
private fun ActionPickerDialog(
    gesture: String,
    current: String,
    onPick: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(24.dp)
                .neonFrame(NeonCyan)
                .padding(16.dp),
        ) {
            NeonText(
                text = "MAP " + prettyGesture(gesture),
                color = NeonCyan,
                style = MaterialTheme.typography.labelMedium,
                blur = 8f,
            )
            Spacer(Modifier.height(10.dp))
            Column(
                modifier = Modifier
                    .heightIn(max = 420.dp)
                    .verticalScroll(rememberScrollState()),
            ) {
                PICKER_ACTIONS.forEach { action ->
                    val selected = action == current
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(3.dp))
                            .background(
                                if (selected) NeonCyan.copy(alpha = 0.12f) else Color.Transparent
                            )
                            .clickable { onPick(action) }
                            .padding(vertical = 10.dp, horizontal = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = prettyAction(action),
                            style = MaterialTheme.typography.bodyMedium,
                            color = if (selected) NeonCyan else TextBody,
                            modifier = Modifier.weight(1f),
                        )
                        if (selected) {
                            Text("OK", style = MaterialTheme.typography.labelSmall, color = NeonCyan)
                        }
                    }
                }
            }
            Spacer(Modifier.height(10.dp))
            NeonButton(
                text = "Cancel",
                accent = NeonRed,
                onClick = onDismiss,
                modifier = Modifier.fillMaxWidth(),
                height = 40.dp,
            )
        }
    }
}

internal fun prettyGesture(id: String): String = id.replace('_', ' ')

internal fun prettyAction(id: String): String = when (id) {
    "play_pause" -> "PLAY / PAUSE"
    "next" -> "NEXT TRACK"
    "prev" -> "PREV TRACK"
    "volume_up" -> "VOLUME +"
    "volume_down" -> "VOLUME -"
    "mute" -> "MUTE"
    "brightness_up" -> "BRIGHTER"
    "brightness_down" -> "DIMMER"
    "stop" -> "STOP"
    "mouse_left" -> "MOUSE LEFT"
    "mouse_right" -> "MOUSE RIGHT"
    "mouse_up" -> "MOUSE UP"
    "mouse_down" -> "MOUSE DOWN"
    "mouse_click" -> "LEFT CLICK"
    "mouse_rclick" -> "RIGHT CLICK"
    "none" -> "NONE"
    else -> id.uppercase().replace('_', ' ')
}
