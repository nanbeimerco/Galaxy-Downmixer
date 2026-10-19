package com.galaxydownmixer.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.unit.dp

const val METER_MIN_DB = -60f
const val METER_MAX_DB = 6f

/**
 * Horizontal level meter: translucent bar = peak, solid bar = RMS, tick =
 * peak hold, thin line = 0 dBFS. Turns tertiary above -6 dBFS and error
 * above 0 dBFS.
 */
@Composable
fun LevelMeter(level: MeterLevel, modifier: Modifier = Modifier, dimmed: Boolean = false) {
    val scheme = MaterialTheme.colorScheme
    val color = when {
        dimmed -> scheme.outline
        level.holdDb > 0f -> scheme.error
        level.peakDb > -6f -> scheme.tertiary
        else -> scheme.primary
    }
    val track = scheme.surfaceContainerHighest
    val marker = scheme.onSurfaceVariant.copy(alpha = 0.5f)

    Canvas(modifier.fillMaxWidth().height(14.dp)) {
        val radius = CornerRadius(size.height / 2)
        fun x(db: Float) = ((db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB)).coerceIn(0f, 1f) * size.width

        drawRoundRect(track, cornerRadius = radius)
        val peakX = x(level.peakDb)
        if (peakX > 0f) drawRoundRect(color.copy(alpha = 0.4f), size = Size(peakX, size.height), cornerRadius = radius)
        val rmsX = x(level.rmsDb)
        if (rmsX > 0f) drawRoundRect(color, size = Size(rmsX, size.height), cornerRadius = radius)

        if (level.holdDb > METER_MIN_DB) {
            val w = 3.dp.toPx()
            drawRect(color, topLeft = Offset(x(level.holdDb) - w / 2, 0f), size = Size(w, size.height))
        }
        val zero = x(0f)
        drawLine(marker, Offset(zero, 0f), Offset(zero, size.height), strokeWidth = 1.dp.toPx())
    }
}

fun formatDb(db: Float): String = if (db <= METER_MIN_DB) "-∞" else "%.1f".format(db)
