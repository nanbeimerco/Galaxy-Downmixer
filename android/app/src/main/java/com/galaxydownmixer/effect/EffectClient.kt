package com.galaxydownmixer.effect

import android.media.audiofx.AudioEffect
import java.lang.reflect.Constructor
import java.lang.reflect.Method
import java.util.UUID

/**
 * Control handle on the Galaxy Downmixer effect.
 *
 * The app creates its own instance on a private audio session that never
 * plays anything. Settings sent to it are global inside the effect library,
 * so they reach the instance that is processing Apple Music; status and meter
 * reads report that most recently active instance.
 *
 * AudioEffect's UUID constructor and byte[] parameter methods are hidden
 * framework API, reached through reflection.
 */
class EffectClient private constructor(private val effect: AudioEffect) : AutoCloseable {

    fun getInt(key: Int): Int? = get(key, 4)?.let { Protocol.le(it).int }

    fun getFloat(key: Int): Float? = get(key, 4)?.let { Protocol.le(it).float }

    fun getBytes(key: Int, capacity: Int): ByteArray? = get(key, capacity)

    fun setInt(key: Int, value: Int): Boolean =
        set(key, ByteArray(4).also { Protocol.le(it).putInt(value) })

    fun setFloat(key: Int, value: Float): Boolean =
        set(key, ByteArray(4).also { Protocol.le(it).putFloat(value) })

    fun setBytes(key: Int, value: ByteArray): Boolean = set(key, value)

    /** Uploads Equalizer APO text in chunks and applies it. */
    fun uploadEq(text: ByteArray): Boolean {
        if (text.size > Protocol.EQ_TEXT_MAX) return false
        if (!setInt(Protocol.PARAM_EQ_TEXT_BEGIN, text.size)) return false
        var pos = 0
        while (pos < text.size) {
            val end = minOf(pos + Protocol.EQ_CHUNK_MAX, text.size)
            if (!setBytes(Protocol.PARAM_EQ_TEXT_CHUNK, text.copyOfRange(pos, end))) return false
            pos = end
        }
        return setInt(Protocol.PARAM_EQ_TEXT_COMMIT, 0)
    }

    override fun close() {
        runCatching { effect.release() }
    }

    private fun key(key: Int) = ByteArray(4).also { Protocol.le(it).putInt(key) }

    private fun get(key: Int, capacity: Int): ByteArray? {
        val value = ByteArray(capacity)
        val n = getParameter.invoke(effect, key(key), value) as Int
        return if (n >= 0) value.copyOf(n) else null
    }

    private fun set(key: Int, value: ByteArray): Boolean =
        (setParameter.invoke(effect, key(key), value) as Int) == 0

    companion object {
        private val constructor: Constructor<AudioEffect> by lazy {
            AudioEffect::class.java.getDeclaredConstructor(
                UUID::class.java, UUID::class.java, Int::class.javaPrimitiveType, Int::class.javaPrimitiveType,
            ).apply { isAccessible = true }
        }
        private val setParameter: Method by lazy {
            AudioEffect::class.java.getDeclaredMethod("setParameter", ByteArray::class.java, ByteArray::class.java)
        }
        private val getParameter: Method by lazy {
            AudioEffect::class.java.getDeclaredMethod("getParameter", ByteArray::class.java, ByteArray::class.java)
        }

        /** True when the effect library is registered with the audio framework. */
        fun isInstalled(): Boolean =
            AudioEffect.queryEffects().orEmpty().any { it.uuid == Protocol.EFFECT_UUID }

        /** @throws Exception when the effect cannot be created or reflection is blocked. */
        fun open(sessionId: Int): EffectClient {
            val effect = constructor.newInstance(
                Protocol.TYPE_UUID, Protocol.EFFECT_UUID, Int.MAX_VALUE, sessionId,
            )
            return EffectClient(effect)
        }
    }
}
