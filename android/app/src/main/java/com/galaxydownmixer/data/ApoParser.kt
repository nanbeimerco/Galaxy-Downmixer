package com.galaxydownmixer.data

import java.util.Locale
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.sqrt

data class EqFilter(
    val index: Int,
    val enabled: Boolean,
    val type: String,
    val freqHz: Float,
    val gainDb: Float,
    val q: Float,
)

data class EqSection(
    val preampDb: Float = 0f,
    val filters: List<EqFilter> = emptyList(),
)

data class ParsedEqProfile(
    val common: EqSection = EqSection(),
    val left: EqSection = EqSection(),
    val right: EqSection = EqSection(),
    val warnings: List<String> = emptyList(),
) {
    val isEmpty: Boolean get() = common.filters.isEmpty() && left.filters.isEmpty() && right.filters.isEmpty()
    val totalFilters: Int get() = common.filters.size + left.filters.size + right.filters.size
}

object ApoParser {

    private enum class TargetChannel { COMMON, LEFT, RIGHT }

    fun parse(text: String): ParsedEqProfile {
        var currentChannel = TargetChannel.COMMON

        var commonPreamp = 0f
        var leftPreamp = 0f
        var rightPreamp = 0f

        val commonFilters = mutableListOf<EqFilter>()
        val leftFilters = mutableListOf<EqFilter>()
        val rightFilters = mutableListOf<EqFilter>()
        val warnings = mutableListOf<String>()

        var filterCounter = 1

        for ((lineIdx, rawLine) in text.lines().withIndex()) {
            val lineNum = lineIdx + 1
            val line = rawLine.trim()
            if (line.isEmpty() || line.startsWith("#")) continue

            val lower = line.lowercase(Locale.ROOT)

            if (lower.startsWith("channel:")) {
                val chArg = lower.removePrefix("channel:").trim()
                currentChannel = when {
                    chArg in listOf("all", "l r", "r l", "1 2", "2 1") -> TargetChannel.COMMON
                    chArg in listOf("l", "1") -> TargetChannel.LEFT
                    chArg in listOf("r", "2") -> TargetChannel.RIGHT
                    else -> {
                        warnings.add("line $lineNum: unknown channel '$chArg', defaulting to common")
                        TargetChannel.COMMON
                    }
                }
                continue
            }

            if (lower.startsWith("preamp:")) {
                val preampStr = lower.removePrefix("preamp:").replace("db", "").trim()
                val preampVal = preampStr.toFloatOrNull()
                if (preampVal != null) {
                    when (currentChannel) {
                        TargetChannel.COMMON -> commonPreamp = preampVal
                        TargetChannel.LEFT -> leftPreamp = preampVal
                        TargetChannel.RIGHT -> rightPreamp = preampVal
                    }
                } else {
                    warnings.add("line $lineNum: invalid preamp value '$preampStr'")
                }
                continue
            }

            // e.g. "Filter 1: ON PK Fc 31 Hz Gain -0.75 dB Q 1.414"
            // or "Filter: ON PK Fc 31 Hz Gain -0.75 dB Q 1.414"
            if (lower.startsWith("filter")) {
                val colonIdx = line.indexOf(':')
                val body = if (colonIdx != -1) line.substring(colonIdx + 1).trim() else line.substringAfter(" ").trim()
                val tokens = body.split(Regex("\\s+"))

                if (tokens.size < 2) {
                    warnings.add("line $lineNum: malformed filter directive")
                    continue
                }

                var idx = 0
                val enabled = when (tokens[idx].uppercase(Locale.ROOT)) {
                    "ON" -> { idx++; true }
                    "OFF" -> { idx++; false }
                    else -> true
                }

                if (idx >= tokens.size) {
                    warnings.add("line $lineNum: missing filter type")
                    continue
                }

                val rawType = tokens[idx++].uppercase(Locale.ROOT)
                val type = when (rawType) {
                    "PK", "PEQ", "MODAL" -> "PK"
                    "LSC", "LS" -> "LSC"
                    "HSC", "HS" -> "HSC"
                    "LP", "LPQ" -> "LP"
                    "HP", "HPQ" -> "HP"
                    "BP" -> "BP"
                    "NO" -> "NO"
                    "AP" -> "AP"
                    else -> rawType
                }

                var freqHz = 1000f
                var gainDb = 0f
                var q = 0.7071f

                while (idx < tokens.size) {
                    val key = tokens[idx++].lowercase(Locale.ROOT)
                    when (key) {
                        "fc" -> {
                            if (idx < tokens.size) {
                                val v = tokens[idx++].replace("hz", "", ignoreCase = true).toFloatOrNull()
                                if (v != null) freqHz = v
                            }
                            if (idx < tokens.size && tokens[idx].equals("hz", ignoreCase = true)) idx++
                        }
                        "gain" -> {
                            if (idx < tokens.size) {
                                val v = tokens[idx++].replace("db", "", ignoreCase = true).toFloatOrNull()
                                if (v != null) gainDb = v
                            }
                            if (idx < tokens.size && tokens[idx].equals("db", ignoreCase = true)) idx++
                        }
                        "q" -> {
                            if (idx < tokens.size) {
                                val v = tokens[idx++].toFloatOrNull()
                                if (v != null) q = v
                            }
                        }
                        "bw" -> {
                            if (idx < tokens.size && tokens[idx].equals("oct", ignoreCase = true)) idx++
                            if (idx < tokens.size) {
                                val bw = tokens[idx++].toFloatOrNull()
                                if (bw != null && bw > 0f) {
                                    val twoPowBw = 2.0.pow(bw.toDouble())
                                    q = (sqrt(twoPowBw) / (twoPowBw - 1.0)).toFloat()
                                }
                            }
                        }
                    }
                }

                val filter = EqFilter(
                    index = filterCounter++,
                    enabled = enabled,
                    type = type,
                    freqHz = freqHz,
                    gainDb = gainDb,
                    q = q,
                )

                when (currentChannel) {
                    TargetChannel.COMMON -> commonFilters.add(filter)
                    TargetChannel.LEFT -> leftFilters.add(filter)
                    TargetChannel.RIGHT -> rightFilters.add(filter)
                }
            } else {
                warnings.add("line $lineNum: unsupported command '${line.take(30)}'")
            }
        }

        return ParsedEqProfile(
            common = EqSection(preampDb = commonPreamp, filters = commonFilters),
            left = EqSection(preampDb = leftPreamp, filters = leftFilters),
            right = EqSection(preampDb = rightPreamp, filters = rightFilters),
            warnings = warnings,
        )
    }
}
