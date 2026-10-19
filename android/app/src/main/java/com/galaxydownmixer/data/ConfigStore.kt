package com.galaxydownmixer.data

import java.util.concurrent.TimeUnit

/**
 * Persists settings into the Magisk module, so the effect loads them by
 * itself after a reboot or an audio HAL restart (no app needed then).
 *
 * Files are rewritten in place ("cat >"): Magisk bind-mounts them over
 * /vendor/etc/galaxy_downmixer, and an in-place write keeps that mount valid.
 */
object ConfigStore {
    private const val DIR = "/data/adb/modules/galaxy_downmixer/system/vendor/etc/galaxy_downmixer"
    const val OPTIONS = "$DIR/options.conf"
    const val EQ = "$DIR/eq.txt"
    private const val LABEL = "u:object_r:vendor_configs_file:s0"

    data class Options(
        val enabled: Boolean,
        val swap: Boolean,
        val muteNames: List<String>,
        val masterDb: Float,
        val lfeLowPass: Boolean,
        val commonPeq: Boolean,
        val earPeq: Boolean,
    )

    fun optionsText(o: Options): String = buildString {
        appendLine("# Written by the Galaxy Downmixer app.")
        appendLine("enabled=${if (o.enabled) 1 else 0}")
        appendLine("swap=${if (o.swap) 1 else 0}")
        appendLine("mute=${o.muteNames.joinToString(",")}")
        appendLine("master_db=${"%.2f".format(java.util.Locale.ROOT, o.masterDb)}")
        appendLine("lfe_lpf=${if (o.lfeLowPass) 1 else 0}")
        appendLine("common_peq=${if (o.commonPeq) 1 else 0}")
        appendLine("ear_peq=${if (o.earPeq) 1 else 0}")
        appendLine("eq_file=/vendor/etc/galaxy_downmixer/eq.txt")
        appendLine("log_interval_s=0")
    }

    /** Returns null on success, or an error message. */
    fun write(path: String, content: ByteArray): String? {
        val script = "cat > '$path' && chcon $LABEL '$path' && chmod 0644 '$path'"
        return runSu(script, content)
    }

    fun read(path: String): ByteArray? {
        val p = runCatching { ProcessBuilder("su", "-c", "cat '$path'").start() }.getOrNull() ?: return null
        val bytes = p.inputStream.readBytes()
        return if (p.waitFor(5, TimeUnit.SECONDS) && p.exitValue() == 0) bytes else null
    }

    fun moduleInstalled(): Boolean = runSu("[ -d '$DIR' ]", ByteArray(0)) == null

    private fun runSu(script: String, stdin: ByteArray): String? {
        val p = try {
            ProcessBuilder("su", "-c", script).redirectErrorStream(true).start()
        } catch (e: Exception) {
            return "root 権限を取得できません (${e.message})"
        }
        p.outputStream.use { it.write(stdin) }
        val output = p.inputStream.bufferedReader().readText().trim()
        if (!p.waitFor(10, TimeUnit.SECONDS)) {
            p.destroy()
            return "root コマンドがタイムアウトしました"
        }
        return if (p.exitValue() == 0) null else output.ifEmpty { "root コマンドが失敗しました (${p.exitValue()})" }
    }
}
