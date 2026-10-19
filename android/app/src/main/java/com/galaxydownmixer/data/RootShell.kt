package com.galaxydownmixer.data

import java.io.BufferedReader
import java.io.OutputStream
import java.util.Base64
import java.util.concurrent.TimeUnit

/**
 * One long-lived `su` process for the whole app session, so Magisk asks (and
 * toasts) once instead of on every settings change. Not thread safe: callers
 * serialise access (ConfigStore does).
 */
class RootShell private constructor(
    private val process: Process,
    private val stdin: OutputStream,
    private val stdout: BufferedReader,
) {
    /** Runs a shell snippet; returns its exit status and combined output. */
    fun run(script: String): Pair<Int, String> {
        val marker = "__gdm_done_${System.nanoTime()}__"
        stdin.write("{ $script ; } 2>&1; echo \"$marker \$?\"\n".toByteArray())
        stdin.flush()
        val out = StringBuilder()
        while (true) {
            val line = stdout.readLine() ?: return -1 to out.toString().trim()
            if (line.startsWith(marker)) {
                val status = line.removePrefix(marker).trim().toIntOrNull() ?: -1
                return status to out.toString().trim()
            }
            out.appendLine(line)
        }
    }

    /** Writes `content` to `path` in place (keeps the inode Magisk bind-mounted). */
    fun writeFile(path: String, content: ByteArray): Pair<Int, String> {
        val b64 = Base64.getEncoder().encodeToString(content)
        return run("echo '$b64' | base64 -d > '$path'")
    }

    val isAlive: Boolean get() = process.isAlive

    fun close() {
        runCatching { stdin.close() }
        process.destroy()
    }

    companion object {
        /**
         * Starts `su` and checks it really runs as root. Returns null when root is
         * unavailable or the request was denied (Magisk then exits the process).
         */
        fun open(): RootShell? {
            val p = try {
                ProcessBuilder("su").redirectErrorStream(true).start()
            } catch (e: Exception) {
                return null
            }
            val shell = RootShell(p, p.outputStream, p.inputStream.bufferedReader())
            val probe = runCatching {
                // Magisk shows its prompt now; give the user time to answer it.
                val result = java.util.concurrent.Executors.newSingleThreadExecutor().let { ex ->
                    try {
                        ex.submit<Pair<Int, String>> { shell.run("id -u") }.get(30, TimeUnit.SECONDS)
                    } finally {
                        ex.shutdownNow()
                    }
                }
                result.first == 0 && result.second.lines().lastOrNull()?.trim() == "0"
            }.getOrDefault(false)
            if (!probe) {
                shell.close()
                return null
            }
            return shell
        }
    }
}
