package com.galaxydownmixer.data

/**
 * Persists settings into the Magisk module, so the effect loads them by
 * itself after a reboot or an audio HAL restart (no app needed then).
 *
 * Files are rewritten in place: Magisk bind-mounts them over
 * /vendor/etc/galaxy_downmixer, and an in-place write keeps that mount valid.
 *
 * Root is requested once. If it is denied, nothing is retried until
 * [retryRoot] is called, so the user is not flooded with Magisk prompts/toasts.
 */
object ConfigStore {
    private const val DIR = "/data/adb/modules/galaxy_downmixer/system/vendor/etc/galaxy_downmixer"
    const val OPTIONS = "$DIR/options.conf"
    const val EQ = "$DIR/eq.txt"
    private const val LABEL = "u:object_r:vendor_configs_file:s0"

    enum class Root { UNKNOWN, GRANTED, DENIED }

    data class Options(
        val enabled: Boolean,
        val swap: Boolean,
        val muteNames: List<String>,
        val masterDb: Float,
        val lfeLowPass: Boolean,
        val commonPeq: Boolean,
        val earPeq: Boolean,
    )

    sealed interface Result {
        data object Ok : Result
        data object NoRoot : Result
        data class Failed(val message: String) : Result
    }

    @Volatile var root: Root = Root.UNKNOWN
        private set

    private var shell: RootShell? = null
    private val lock = Any()

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

    /** Blocking: may wait for the Magisk prompt the first time. */
    fun write(path: String, content: ByteArray): Result = synchronized(lock) {
        val sh = shellLocked() ?: return Result.NoRoot
        val (status, output) = sh.writeFile(path, content)
        if (status != 0) {
            if (!sh.isAlive) dropShellLocked()
            return Result.Failed(output.ifEmpty { "書き込みに失敗しました ($status)" })
        }
        sh.run("chcon $LABEL '$path'; chmod 0644 '$path'")
        Result.Ok
    }

    /** Runs an arbitrary command via the root shell. */
    fun run(script: String): Pair<Int, String>? = synchronized(lock) {
        val sh = shellLocked() ?: return null
        sh.run(script)
    }

    /** Asks Magisk once (if not asked yet) and reports the outcome. Blocking. */
    fun ensureRoot(): Root = synchronized(lock) {
        shellLocked()
        root
    }

    /** Clears a previous denial and asks Magisk again. Blocking. */
    fun retryRoot(): Root = synchronized(lock) {
        dropShellLocked()
        root = Root.UNKNOWN
        shellLocked()
        root
    }

    private fun shellLocked(): RootShell? {
        shell?.takeIf { it.isAlive }?.let { return it }
        if (root == Root.DENIED) return null
        val sh = RootShell.open()
        shell = sh
        root = if (sh != null) Root.GRANTED else Root.DENIED
        return sh
    }

    private fun dropShellLocked() {
        shell?.close()
        shell = null
    }
}
