package com.qd023.glove.ui.theme

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shadow
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

private val GloveColorScheme = darkColorScheme(
    primary = NeonCyan,
    onPrimary = BgDeep,
    primaryContainer = NeonCyan.copy(alpha = 0.15f),
    onPrimaryContainer = TextBright,
    secondary = NeonMagenta,
    onSecondary = BgDeep,
    tertiary = NeonGreen,
    onTertiary = BgDeep,
    background = BgDeep,
    onBackground = TextBody,
    surface = PanelBottom,
    onSurface = TextBright,
    surfaceVariant = PanelTop,
    onSurfaceVariant = TextDim,
    outline = NeonCyan.copy(alpha = 0.4f),
    error = NeonRed,
    onError = BgDeep,
)

@Composable
fun GloveTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = GloveColorScheme,
        typography = GloveTypography,
        content = content,
    )
}

// ---- modifiers ------------------------------------------------------------

/** Faint CRT scanlines, drawn above the panel fill and below the content. */
fun Modifier.scanlines(alpha: Float = 0.028f): Modifier = drawBehind {
    val step = 3.dp.toPx()
    var y = 0f
    while (y < size.height) {
        drawLine(
            color = Color.White.copy(alpha = alpha),
            start = Offset(0f, y),
            end = Offset(size.width, y),
            strokeWidth = 1f,
        )
        y += step
    }
}

/** Dark glass panel with a neon rim and an outer glow. */
fun Modifier.neonFrame(
    accent: Color,
    shape: Shape = RoundedCornerShape(6.dp),
): Modifier = this
    .shadow(12.dp, shape, clip = false, ambientColor = accent, spotColor = accent)
    .background(Brush.verticalGradient(listOf(PanelTop, PanelBottom)), shape)
    .border(1.dp, accent.copy(alpha = 0.55f), shape)
    .scanlines()

private fun glowStyle(base: TextStyle, color: Color, blur: Float, alpha: Float = 0.85f) =
    base.copy(shadow = Shadow(color = color.copy(alpha = alpha), blurRadius = blur))

// ---- components -----------------------------------------------------------

@Composable
fun NeonText(
    text: String,
    color: Color,
    style: TextStyle,
    modifier: Modifier = Modifier,
    blur: Float = 10f,
) {
    Text(
        text = text,
        modifier = modifier,
        style = glowStyle(style, color, blur),
        color = color,
    )
}

@Composable
fun NeonPanel(
    title: String,
    accent: Color = NeonCyan,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .neonFrame(accent)
            .padding(horizontal = 14.dp, vertical = 12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(6.dp)
                    .background(accent, RoundedCornerShape(1.dp))
            )
            Spacer(Modifier.width(8.dp))
            NeonText(
                text = title.uppercase(),
                color = accent,
                style = MaterialTheme.typography.labelMedium,
                blur = 8f,
            )
            Spacer(Modifier.weight(1f))
            Text(
                text = "///",
                style = MaterialTheme.typography.labelSmall,
                color = accent.copy(alpha = 0.35f),
            )
        }
        Spacer(Modifier.height(8.dp))
        Box(
            Modifier
                .fillMaxWidth()
                .height(1.dp)
                .background(
                    Brush.horizontalGradient(
                        listOf(accent.copy(alpha = 0.5f), accent.copy(alpha = 0.04f))
                    )
                )
        )
        Spacer(Modifier.height(10.dp))
        content()
    }
}

/** Label on the left, monospaced value on the right - always aligned. */
@Composable
fun NeonStat(
    label: String,
    value: String,
    accent: Color = NeonCyan,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            color = TextDim,
            modifier = Modifier.weight(1f),
        )
        Text(
            text = value,
            style = ReadoutStyle,
            color = accent,
            maxLines = 1,
        )
    }
}

/** Filled neon action button with a fixed height so rows stay aligned. */
@Composable
fun NeonButton(
    text: String,
    accent: Color,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    height: Dp = 50.dp,
    enabled: Boolean = true,
) {
    val shape = RoundedCornerShape(4.dp)
    val rim = if (enabled) accent else TextDim
    Box(
        modifier = modifier
            .height(height)
            .clip(shape)
            .background(
                Brush.verticalGradient(
                    listOf(accent.copy(alpha = if (enabled) 0.20f else 0.04f), accent.copy(alpha = 0.03f))
                )
            )
            .border(1.dp, rim.copy(alpha = 0.75f), shape)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text.uppercase(),
            style = glowStyle(MaterialTheme.typography.labelLarge, accent, 6f, 0.6f),
            color = if (enabled) TextBright else TextDim,
            maxLines = 1,
        )
    }
}

/** Compact secondary action used inside list rows. */
@Composable
fun NeonMiniButton(
    text: String,
    accent: Color,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
) {
    val shape = RoundedCornerShape(3.dp)
    val rim = if (enabled) accent else TextDim
    Box(
        modifier = modifier
            .width(72.dp)
            .height(28.dp)
            .clip(shape)
            .background(accent.copy(alpha = if (enabled) 0.10f else 0.03f))
            .border(1.dp, rim.copy(alpha = 0.6f), shape)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text.uppercase(),
            style = MaterialTheme.typography.labelSmall,
            color = if (enabled) accent else TextDim,
            maxLines = 1,
        )
    }
}

/** Compact square button used by steppers and header controls. */
@Composable
fun NeonSquare(
    text: String,
    accent: Color,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    size: Dp = 30.dp,
    enabled: Boolean = true,
) {
    val shape = RoundedCornerShape(3.dp)
    val rim = if (enabled) accent else TextDim
    Box(
        modifier = modifier
            .size(size)
            .clip(shape)
            .background(accent.copy(alpha = if (enabled) 0.10f else 0.03f))
            .border(1.dp, rim.copy(alpha = 0.6f), shape)
            .clickable(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall,
            color = if (enabled) accent else TextDim,
            maxLines = 1,
        )
    }
}

@Composable
fun StatusChip(ok: Boolean, modifier: Modifier = Modifier) {
    val c = if (ok) NeonGreen else NeonRed
    Box(
        modifier = modifier
            .clip(RoundedCornerShape(2.dp))
            .background(c.copy(alpha = 0.12f))
            .border(1.dp, c.copy(alpha = 0.65f), RoundedCornerShape(2.dp))
            .padding(horizontal = 6.dp, vertical = 1.dp),
    ) {
        Text(
            text = if (ok) "OK" else "OFF",
            style = MaterialTheme.typography.labelSmall,
            color = c,
        )
    }
}

/** Vertical bar gauge used for the five finger channels. */
@Composable
fun FingerGauge(
    label: String,
    value: Int,
    accent: Color,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(5.dp),
    ) {
        Box(
            Modifier
                .width(16.dp)
                .height(38.dp)
                .clip(RoundedCornerShape(3.dp))
                .background(TrackDark)
                .border(1.dp, accent.copy(alpha = 0.30f), RoundedCornerShape(3.dp))
        ) {
            val frac = value.coerceIn(0, 100) / 100f
            if (frac > 0f) {
                Box(
                    Modifier
                        .align(Alignment.BottomCenter)
                        .fillMaxWidth()
                        .fillMaxHeight(frac)
                        .background(
                            Brush.verticalGradient(
                                listOf(accent, accent.copy(alpha = 0.30f))
                            )
                        )
                )
            }
        }
        Text(label, style = MaterialTheme.typography.labelSmall, color = TextDim)
        Text("$value", style = MaterialTheme.typography.labelSmall, color = accent)
    }
}
