package com.galaxydownmixer.ui

import android.app.Application
import android.content.Context
import android.media.AudioManager
import android.net.Uri
import android.provider.OpenableColumns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.galaxydownmixer.data.ConfigStore
import com.galaxydownmixer.effect.Channels
import com.galaxydownmixer.effect.EffectClient
import com.galaxydownmixer.effect.EffectStatus
import com.galaxydownmixer.effect.EqSummary
import com.galaxydownmixer.effect.MeterFrame
import com.galaxydownmixer.effect.Protocol
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.concurrent.Executors
import kotlin.math.log10
import kotlin.math.max

sealed interface Connection {
    data object Checking : Connection
    data object NotInstalled : Connection
    data class Failed(val message: String) : Connection
    data object Connected : Connection
}

/** One meter, in dBFS, already smoothed for display. */
data class MeterLevel(val peakDb: Float, val rmsDb: Float, val holdDb: Float) {
    companion object {
        val SILENT = MeterLevel(FLOOR_DB, FLOOR_DB, FLOOR_DB)
    }
}

data class ChannelRow(val bit: Int, val level: MeterLevel, val muted: Boolean)

data class UiState(
    val connection: Connection = Connection.Checking,
    val status: EffectStatus? = null,
    val playing: Boolean = false,
    val inputs: List<ChannelRow> = emptyList(),
    val mix: List<MeterLevel> = listOf(MeterLevel.SILENT, MeterLevel.SILENT),
    val out: List<MeterLevel> = listOf(MeterLevel.SILENT, MeterLevel.SILENT),
    val clippedSamples: Long = 0,
    val enabled: Boolean = true,
    val swap: Boolean = false,
    val masterDb: Float = 0f,
    val lfeLowPass: Boolean = true,
    val commonPeq: Boolean = true,
    val earPeq: Boolean = true,
    val muteMask: Int = 0,
    val eq: EqSummary = EqSummary.EMPTY,
    val eqWarnings: List<String> = emptyList(),
    val eqName: String? = null,
    val message: String? = null,
)

const val FLOOR_DB = -90f

class DownmixViewModel(app: Application) : AndroidViewModel(app) {

    private val _state = MutableStateFlow(UiState())
    val state: StateFlow<UiState> = _state.asStateFlow()

    // Every effect call goes through one thread: AudioEffect is not thread safe.
    private val effectThread = Executors.newSingleThreadExecutor { Thread(it, "effect-client") }
        .asCoroutineDispatcher()
    private val prefs = app.getSharedPreferences("downmixer", Context.MODE_PRIVATE)

    private var client: EffectClient? = null
    private var pollJob: Job? = null
    private var persistJob: Job? = null
    private var failures = 0
    private var lastFrames = -1L

    // Display ballistics, indexed like the meter frame (inputs), then mix L/R, out L/R.
    private val peak = FloatArray(36) { FLOOR_DB }
    private val hold = FloatArray(36) { FLOOR_DB }
    private val holdAgeMs = LongArray(36)

    init {
        _state.update { it.copy(eqName = prefs.getString(KEY_EQ_NAME, null)) }
        connect()
    }

    fun connect() {
        viewModelScope.launch(effectThread) {
            _state.update { it.copy(connection = Connection.Checking) }
            client?.close()
            client = null
            val result = runCatching {
                if (!EffectClient.isInstalled()) return@runCatching Connection.NotInstalled
                val am = getApplication<Application>().getSystemService(AudioManager::class.java)
                val c = EffectClient.open(am.generateAudioSessionId())
                val version = c.getInt(Protocol.PARAM_VERSION)
                if (version != Protocol.VERSION) {
                    c.close()
                    return@runCatching Connection.Failed("エフェクトのプロトコル版が違います (アプリ ${Protocol.VERSION} / エフェクト $version)。モジュールを更新してください")
                }
                client = c
                failures = 0
                syncFromEffect(c)
                Connection.Connected
            }.getOrElse { Connection.Failed("エフェクトに接続できません: ${it.cause?.message ?: it.message}") }
            _state.update { it.copy(connection = result) }
        }
    }

    /** Called from the UI's lifecycle: meters are only polled while visible. */
    fun setVisible(visible: Boolean) {
        pollJob?.cancel()
        pollJob = if (visible) viewModelScope.launch(effectThread) { pollLoop() } else null
    }

    private suspend fun pollLoop() {
        var lastStatusMs = 0L
        var lastTick = System.nanoTime()
        while (currentCoroutineContextActive()) {
            val c = client
            if (c != null) {
                val now = System.nanoTime()
                val dtMs = (now - lastTick) / 1_000_000
                lastTick = now
                val meters = c.getBytes(Protocol.PARAM_METERS, Protocol.METERS_SIZE)?.let(MeterFrame::parse)
                if (meters == null) {
                    if (++failures > 30) reconnect()
                } else {
                    failures = 0
                    applyMeters(meters, dtMs)
                }
                val nowMs = now / 1_000_000
                if (nowMs - lastStatusMs > 500) {
                    lastStatusMs = nowMs
                    c.getBytes(Protocol.PARAM_STATUS, Protocol.STATUS_SIZE)?.let(EffectStatus::parse)?.let { s ->
                        val playing = lastFrames >= 0 && s.framesProcessed != lastFrames
                        lastFrames = s.framesProcessed
                        _state.update { it.copy(status = s, playing = playing) }
                    }
                }
            }
            delay(33)
        }
    }

    private suspend fun currentCoroutineContextActive() = kotlin.coroutines.coroutineContext.isActive

    private fun reconnect() {
        failures = 0
        connect()
    }

    private fun applyMeters(m: MeterFrame, dtMs: Long) {
        val fall = 24f * dtMs / 1000f  // dB per second of fall-back
        fun smooth(i: Int, linearPeak: Float, linearRms: Float): MeterLevel {
            val p = toDb(linearPeak)
            peak[i] = max(p, peak[i] - fall)
            holdAgeMs[i] += dtMs
            if (p >= hold[i] || holdAgeMs[i] > 1500) {
                hold[i] = p
                holdAgeMs[i] = 0
            }
            return MeterLevel(peak[i], toDb(linearRms), max(hold[i], peak[i]))
        }

        val mask = _state.value.muteMask
        val inputs = m.channelBits.indices.map { i ->
            ChannelRow(m.channelBits[i], smooth(i, m.inPeak[i], m.inRms[i]), mask and m.channelBits[i] != 0)
        }
        val mix = (0..1).map { smooth(32 + it, m.mixPeak[it], m.mixRms[it]) }
        val out = (0..1).map { smooth(34 + it, m.outPeak[it], m.outRms[it]) }
        _state.update {
            it.copy(inputs = inputs, mix = mix, out = out, clippedSamples = it.clippedSamples + m.clippedSamples)
        }
    }

    private fun syncFromEffect(c: EffectClient) {
        _state.update {
            it.copy(
                enabled = c.getInt(Protocol.PARAM_ENABLED)?.let { v -> v != 0 } ?: it.enabled,
                swap = c.getInt(Protocol.PARAM_SWAP)?.let { v -> v != 0 } ?: it.swap,
                masterDb = c.getFloat(Protocol.PARAM_MASTER_GAIN_DB) ?: it.masterDb,
                lfeLowPass = c.getInt(Protocol.PARAM_LFE_LOW_PASS)?.let { v -> v != 0 } ?: it.lfeLowPass,
                commonPeq = c.getInt(Protocol.PARAM_COMMON_PEQ_ENABLED)?.let { v -> v != 0 } ?: it.commonPeq,
                earPeq = c.getInt(Protocol.PARAM_EAR_PEQ_ENABLED)?.let { v -> v != 0 } ?: it.earPeq,
                muteMask = c.getInt(Protocol.PARAM_MUTE_MASK) ?: it.muteMask,
            )
        }
        refreshEq(c)
    }

    private fun refreshEq(c: EffectClient) {
        val summary = c.getBytes(Protocol.PARAM_EQ_SUMMARY, Protocol.EQ_SUMMARY_SIZE)?.let(EqSummary::parse)
        val warnings = c.getBytes(Protocol.PARAM_EQ_WARNINGS, 4096)
            ?.toString(Charsets.UTF_8)?.lines()?.filter { it.isNotBlank() }.orEmpty()
        _state.update { it.copy(eq = summary ?: it.eq, eqWarnings = warnings) }
    }

    // --- actions ----------------------------------------------------------------

    fun setEnabled(v: Boolean) = sendInt(Protocol.PARAM_ENABLED, if (v) 1 else 0) { copy(enabled = v) }
    fun setSwap(v: Boolean) = sendInt(Protocol.PARAM_SWAP, if (v) 1 else 0) { copy(swap = v) }
    fun setLfeLowPass(v: Boolean) = sendInt(Protocol.PARAM_LFE_LOW_PASS, if (v) 1 else 0) { copy(lfeLowPass = v) }
    fun setCommonPeq(v: Boolean) = sendInt(Protocol.PARAM_COMMON_PEQ_ENABLED, if (v) 1 else 0) { copy(commonPeq = v) }
    fun setEarPeq(v: Boolean) = sendInt(Protocol.PARAM_EAR_PEQ_ENABLED, if (v) 1 else 0) { copy(earPeq = v) }

    fun toggleMute(bit: Int) {
        val mask = _state.value.muteMask xor bit
        sendInt(Protocol.PARAM_MUTE_MASK, mask) { copy(muteMask = mask) }
    }

    fun setMasterDb(db: Float) {
        _state.update { it.copy(masterDb = db) }
        viewModelScope.launch(effectThread) {
            client?.setFloat(Protocol.PARAM_MASTER_GAIN_DB, db)
            schedulePersist()
        }
    }

    fun resetClipCount() = _state.update { it.copy(clippedSamples = 0) }

    fun consumeMessage() = _state.update { it.copy(message = null) }

    fun loadEq(uri: Uri) {
        viewModelScope.launch(effectThread) {
            val app = getApplication<Application>()
            val bytes = runCatching {
                app.contentResolver.openInputStream(uri)!!.use { it.readBytes() }
            }.getOrElse {
                toast("ファイルを読めません: ${it.message}")
                return@launch
            }
            if (bytes.size > Protocol.EQ_TEXT_MAX) {
                toast("ファイルが大きすぎます (64 KB まで)")
                return@launch
            }
            val c = client ?: return@launch toast("エフェクトに接続されていません")
            if (!c.uploadEq(bytes)) return@launch toast("EQ を送信できませんでした")
            refreshEq(c)
            val name = displayName(uri)
            prefs.edit().putString(KEY_EQ_NAME, name).apply()
            _state.update { it.copy(eqName = name) }
            val err = withContext(Dispatchers.IO) { ConfigStore.write(ConfigStore.EQ, bytes) }
            toast(err?.let { "EQ を適用しました (保存に失敗: $it)" } ?: "EQ を適用して保存しました")
        }
    }

    fun clearEq() {
        viewModelScope.launch(effectThread) {
            val c = client ?: return@launch
            if (!c.setInt(Protocol.PARAM_EQ_CLEAR, 0)) return@launch toast("EQ を解除できませんでした")
            refreshEq(c)
            prefs.edit().remove(KEY_EQ_NAME).apply()
            _state.update { it.copy(eqName = null) }
            val err = withContext(Dispatchers.IO) {
                ConfigStore.write(ConfigStore.EQ, "# no EQ\n".toByteArray())
            }
            toast(err?.let { "EQ を解除しました (保存に失敗: $it)" } ?: "EQ を解除しました")
        }
    }

    private inline fun sendInt(key: Int, value: Int, crossinline change: UiState.() -> UiState) {
        _state.update { it.change() }
        viewModelScope.launch(effectThread) {
            val ok = client?.setInt(key, value) ?: false
            if (!ok) toast("エフェクトへの設定に失敗しました")
            schedulePersist()
        }
    }

    private fun schedulePersist() {
        persistJob?.cancel()
        persistJob = viewModelScope.launch(Dispatchers.IO) {
            delay(800)
            val s = _state.value
            val text = ConfigStore.optionsText(
                ConfigStore.Options(
                    enabled = s.enabled,
                    swap = s.swap,
                    muteNames = Channels.inMask(s.muteMask).map(Channels::name),
                    masterDb = s.masterDb,
                    lfeLowPass = s.lfeLowPass,
                    commonPeq = s.commonPeq,
                    earPeq = s.earPeq,
                ),
            )
            ConfigStore.write(ConfigStore.OPTIONS, text.toByteArray())?.let { toast("設定を保存できません: $it") }
        }
    }

    private fun toast(msg: String) = _state.update { it.copy(message = msg) }

    private fun displayName(uri: Uri): String {
        val cr = getApplication<Application>().contentResolver
        return runCatching {
            cr.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0) else null
            }
        }.getOrNull() ?: uri.lastPathSegment ?: "EQ"
    }

    override fun onCleared() {
        client?.close()
        effectThread.close()
    }

    private companion object {
        const val KEY_EQ_NAME = "eq_name"

        fun toDb(linear: Float): Float =
            if (linear > 0f) max(20f * log10(linear), FLOOR_DB) else FLOOR_DB
    }
}
