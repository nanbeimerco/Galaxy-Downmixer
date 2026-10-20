package com.galaxydownmixer.effect

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.UUID

/** Mirror of effect/src/Protocol.h. Keep the two in sync. */
object Protocol {
    val TYPE_UUID: UUID = UUID.fromString("24c7d03d-8e80-43c2-94a8-34770cb8db35")
    val EFFECT_UUID: UUID = UUID.fromString("a6e3f002-29bf-40a2-bfcf-df47efb3a224")

    const val VERSION = 1

    const val PARAM_VERSION = 0
    const val PARAM_STATUS = 1
    const val PARAM_METERS = 2
    const val PARAM_ENABLED = 10
    const val PARAM_MUTE_MASK = 11
    const val PARAM_SWAP = 12
    const val PARAM_MASTER_GAIN_DB = 13
    const val PARAM_LFE_LOW_PASS = 14
    const val PARAM_COMMON_PEQ_ENABLED = 15
    const val PARAM_EAR_PEQ_ENABLED = 16
    const val PARAM_RELOAD_FILES = 20
    const val PARAM_EQ_TEXT_BEGIN = 21
    const val PARAM_EQ_TEXT_CHUNK = 22
    const val PARAM_EQ_TEXT_COMMIT = 23
    const val PARAM_EQ_SUMMARY = 24
    const val PARAM_EQ_WARNINGS = 25
    const val PARAM_EQ_CLEAR = 26
    const val PARAM_RECORD_CONTROL = 30
    const val PARAM_RECORD_STATUS = 31

    const val EQ_CHUNK_MAX = 512
    const val EQ_TEXT_MAX = 64 * 1024

    const val STATUS_SIZE = 32
    const val METERS_SIZE = 428
    const val EQ_SUMMARY_SIZE = 32
    const val RECORD_STATUS_SIZE = 24

    internal fun le(bytes: ByteArray): ByteBuffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
}

data class EffectStatus(
    val sampleRate: Int,
    val channelMask: Int,
    val format: Int,
    val enabled: Boolean,
    val framesProcessed: Long,
    val sessionId: Int,
    val instanceCount: Int,
) {
    val channels: List<Int> get() = Channels.inMask(channelMask)

    companion object {
        fun parse(b: ByteArray): EffectStatus {
            val buf = Protocol.le(b)
            return EffectStatus(
                sampleRate = buf.int,
                channelMask = buf.int,
                format = buf.int,
                enabled = buf.int != 0,
                framesProcessed = buf.long,
                sessionId = buf.int,
                instanceCount = buf.int,
            )
        }
    }
}

/** Linear levels (1.0 = 0 dBFS) accumulated since the previous read. */
class MeterFrame(
    val channelBits: IntArray,
    val inPeak: FloatArray,
    val inRms: FloatArray,
    val mixPeak: FloatArray,
    val mixRms: FloatArray,
    val outPeak: FloatArray,
    val outRms: FloatArray,
    val clippedSamples: Int,
    val frames: Int,
) {
    companion object {
        fun parse(b: ByteArray): MeterFrame {
            val buf = Protocol.le(b)
            val n = buf.int.coerceIn(0, 32)
            val bits = IntArray(32) { buf.int }
            val peak = FloatArray(32) { buf.float }
            val rms = FloatArray(32) { buf.float }
            val mixPeak = FloatArray(2) { buf.float }
            val mixRms = FloatArray(2) { buf.float }
            val outPeak = FloatArray(2) { buf.float }
            val outRms = FloatArray(2) { buf.float }
            return MeterFrame(
                channelBits = bits.copyOf(n),
                inPeak = peak.copyOf(n),
                inRms = rms.copyOf(n),
                mixPeak = mixPeak,
                mixRms = mixRms,
                outPeak = outPeak,
                outRms = outRms,
                clippedSamples = buf.int,
                frames = buf.int,
            )
        }
    }
}

data class EqSummary(
    val commonFilters: Int,
    val leftFilters: Int,
    val rightFilters: Int,
    val warningCount: Int,
    val commonPreampDb: Float,
    val leftPreampDb: Float,
    val rightPreampDb: Float,
    val textBytes: Int,
) {
    val isEmpty: Boolean
        get() = commonFilters == 0 && leftFilters == 0 && rightFilters == 0 &&
            commonPreampDb == 0f && leftPreampDb == 0f && rightPreampDb == 0f

    companion object {
        val EMPTY = EqSummary(0, 0, 0, 0, 0f, 0f, 0f, 0)

        fun parse(b: ByteArray): EqSummary {
            val buf = Protocol.le(b)
            return EqSummary(
                commonFilters = buf.int,
                leftFilters = buf.int,
                rightFilters = buf.int,
                warningCount = buf.int,
                commonPreampDb = buf.float,
                leftPreampDb = buf.float,
                rightPreampDb = buf.float,
                textBytes = buf.int,
            )
        }
    }
}

data class RecordStatus(
    val isRecording: Boolean,
    val sampleRate: Int,
    val recordedFrames: Long,
    val droppedFrames: Int,
) {
    val durationSeconds: Double
        get() = if (sampleRate > 0) recordedFrames.toDouble() / sampleRate else 0.0

    companion object {
        val IDLE = RecordStatus(false, 0, 0L, 0)

        fun parse(b: ByteArray): RecordStatus {
            val buf = Protocol.le(b)
            return RecordStatus(
                isRecording = buf.int != 0,
                sampleRate = buf.int,
                recordedFrames = buf.long,
                droppedFrames = buf.int,
            )
        }
    }
}

/** AOSP audio_channel_mask_t bits (same as dsp/include/gdm/ChannelMap.h). */
object Channels {
    const val FL = 0x1
    const val FR = 0x2
    const val FC = 0x4
    const val LFE = 0x8

    private val names = linkedMapOf(
        0x1 to "FL", 0x2 to "FR", 0x4 to "FC", 0x8 to "LFE", 0x10 to "BL", 0x20 to "BR",
        0x40 to "FLC", 0x80 to "FRC", 0x100 to "BC", 0x200 to "SL", 0x400 to "SR",
        0x800 to "TC", 0x1000 to "TFL", 0x2000 to "TFC", 0x4000 to "TFR", 0x8000 to "TBL",
        0x10000 to "TBC", 0x20000 to "TBR", 0x40000 to "TSL", 0x80000 to "TSR",
        0x100000 to "BFL", 0x200000 to "BFC", 0x400000 to "BFR", 0x800000 to "LFE2",
        0x1000000 to "FWL", 0x2000000 to "FWR",
    )

    fun name(bit: Int): String = names[bit] ?: "?"

    fun inMask(mask: Int): List<Int> = (0 until 32).map { 1 shl it }.filter { mask and it != 0 }

    /** "7.1.4" style label for common layouts. */
    fun layoutName(mask: Int): String = when (mask) {
        0x1 -> "モノラル"
        0x3 -> "ステレオ"
        0x3F -> "5.1"
        0x63F -> "7.1"
        0x2D63F -> "7.1.4"
        else -> "${Integer.bitCount(mask)} ch"
    }
}
