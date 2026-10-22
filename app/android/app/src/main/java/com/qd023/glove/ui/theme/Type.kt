package com.qd023.glove.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import com.qd023.glove.R

/** Retro-futuristic display face - used for the title and button caps. */
val ArcadeDisplay = FontFamily(Font(R.font.audiowide_regular, FontWeight.Normal))

/** Clean squarish UI face - used for labels and body copy. */
val TechSans = FontFamily(
    Font(R.font.chakra_petch_regular, FontWeight.Normal),
    Font(R.font.chakra_petch_semibold, FontWeight.SemiBold),
    Font(R.font.chakra_petch_bold, FontWeight.Bold),
)

/** Monospaced readout face - used for live numeric data. */
val TechMono = FontFamily(Font(R.font.share_tech_mono, FontWeight.Normal))

val GloveTypography = Typography(
    displayLarge = TextStyle(
        fontFamily = ArcadeDisplay,
        fontWeight = FontWeight.Normal,
        fontSize = 32.sp,
        letterSpacing = 2.sp,
    ),
    headlineMedium = TextStyle(
        fontFamily = ArcadeDisplay,
        fontWeight = FontWeight.Normal,
        fontSize = 22.sp,
        letterSpacing = 1.5.sp,
    ),
    titleLarge = TextStyle(
        fontFamily = ArcadeDisplay,
        fontWeight = FontWeight.Normal,
        fontSize = 15.sp,
        letterSpacing = 2.sp,
    ),
    titleMedium = TextStyle(
        fontFamily = TechSans,
        fontWeight = FontWeight.SemiBold,
        fontSize = 14.sp,
        letterSpacing = 1.4.sp,
    ),
    bodyLarge = TextStyle(
        fontFamily = TechSans,
        fontWeight = FontWeight.Normal,
        fontSize = 15.sp,
        lineHeight = 20.sp,
    ),
    bodyMedium = TextStyle(
        fontFamily = TechSans,
        fontWeight = FontWeight.Normal,
        fontSize = 13.5.sp,
        lineHeight = 18.sp,
    ),
    bodySmall = TextStyle(
        fontFamily = TechSans,
        fontWeight = FontWeight.Normal,
        fontSize = 12.sp,
        lineHeight = 16.sp,
    ),
    labelLarge = TextStyle(
        fontFamily = ArcadeDisplay,
        fontWeight = FontWeight.Normal,
        fontSize = 12.sp,
        letterSpacing = 1.6.sp,
    ),
    labelMedium = TextStyle(
        fontFamily = TechSans,
        fontWeight = FontWeight.SemiBold,
        fontSize = 12.sp,
        letterSpacing = 1.6.sp,
    ),
    labelSmall = TextStyle(
        fontFamily = TechMono,
        fontWeight = FontWeight.Normal,
        fontSize = 11.sp,
        letterSpacing = 0.5.sp,
    ),
)

/** Monospaced live-data style with a stronger presence. */
val ReadoutStyle = TextStyle(
    fontFamily = TechMono,
    fontWeight = FontWeight.Normal,
    fontSize = 14.sp,
    letterSpacing = 0.6.sp,
)
