package com.galaxydownmixer.ui

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.CallMerge
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.FileOpen
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.SurroundSound
import androidx.compose.material.icons.automirrored.rounded.VolumeOff
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ButtonGroupDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ContainedLoadingIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilledTonalIconToggleButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialShapes
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.ToggleButton
import androidx.compose.material3.ToggleButtonDefaults
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.toShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.galaxydownmixer.data.ConfigStore
import com.galaxydownmixer.effect.Channels
import com.galaxydownmixer.effect.EqSummary
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class, ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun MainScreen(vm: DownmixViewModel) {
    val state by vm.state.collectAsStateWithLifecycle()
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    val snackbar = remember { SnackbarHostState() }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        uri?.let(vm::loadEq)
    }

    LaunchedEffect(state.message) {
        state.message?.let {
            snackbar.showSnackbar(it)
            vm.consumeMessage()
        }
    }

    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text("Galaxy Downmixer") },
                subtitle = { Text(subtitle(state)) },
                scrollBehavior = scrollBehavior,
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { padding ->
        LazyColumn(
            contentPadding = PaddingValues(
                start = 16.dp,
                end = 16.dp,
                top = padding.calculateTopPadding() + 8.dp,
                bottom = padding.calculateBottomPadding() + 32.dp,
            ),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            when (val c = state.connection) {
                Connection.Checking -> item { CheckingCard() }
                Connection.NotInstalled -> item {
                    ProblemCard(
                        title = "エフェクトが見つかりません",
                        body = "Magisk モジュール「Galaxy Downmixer」をインストールして再起動してください。",
                        onRetry = vm::connect,
                    )
                }
                is Connection.Failed -> item { ProblemCard("接続できません", c.message, vm::connect) }
                Connection.Connected -> {
                    if (state.root == ConfigStore.Root.DENIED) item { RootDeniedCard(vm::retryRoot) }
                    item { ModeSelector(state.enabled, vm::setEnabled) }
                    item { InputCard(state, vm::toggleMute) }
                    item { OutputCard(state, vm::resetClipCount) }
                    item { ProcessingCard(state, vm) }
                    item { RecordCard(state, vm::toggleRecording) }
                    item {
                        EqCardView(
                            state = state,
                            onLoad = { picker.launch(arrayOf("text/plain", "application/octet-stream", "*/*")) },
                            onClear = vm::clearEq,
                            onSelectPreset = vm::selectPreset,
                            onDeletePreset = vm::deletePreset,
                            onRenamePreset = vm::renamePreset,
                            onAssignDevice = vm::assignCurrentDevice,
                            onToggleAutoSwitch = vm::setAutoDeviceSwitch,
                        )
                    }
                }
            }
        }
    }
}

private fun subtitle(s: UiState): String {
    val st = s.status
    return when {
        s.connection != Connection.Connected -> "7.1.4 → 2ch ダウンミックス"
        st == null || st.sampleRate == 0 -> "音声を待っています"
        else -> buildString {
            append(Channels.layoutName(st.channelMask))
            append(" · ")
            append(String.format(Locale.ROOT, "%.1f kHz", st.sampleRate / 1000f).replace(".0 ", " "))
            append(if (s.playing) " · 再生中" else " · 停止中")
        }
    }
}

// --- building blocks -------------------------------------------------------------

@Composable
fun SectionCard(title: String, trailing: (@Composable () -> Unit)? = null, content: @Composable ColumnScope.() -> Unit) {
    Card(
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
    ) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(title, style = MaterialTheme.typography.titleMedium, modifier = Modifier.weight(1f))
                trailing?.invoke()
            }
            content()
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun CheckingCard() {
    Column(
        Modifier.fillMaxWidth().padding(top = 48.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        ContainedLoadingIndicator(Modifier.size(72.dp))
        Text("エフェクトを探しています…", style = MaterialTheme.typography.bodyLarge)
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun ProblemCard(title: String, body: String, onRetry: () -> Unit) {
    Card(
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer),
    ) {
        Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            ShapeBadge(Icons.Rounded.ErrorOutline, MaterialTheme.colorScheme.error, MaterialTheme.colorScheme.onError)
            Text(title, style = MaterialTheme.typography.headlineSmall)
            Text(body, style = MaterialTheme.typography.bodyMedium)
            Button(onClick = onRetry, shapes = ButtonDefaults.shapes()) {
                Icon(Icons.Rounded.Refresh, contentDescription = null)
                Spacer(Modifier.width(ButtonDefaults.IconSpacing))
                Text("再試行")
            }
        }
    }
}

/** Shown once (instead of a message per change) while Magisk denies root. */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun RootDeniedCard(onRetry: () -> Unit) {
    Card(
        shape = MaterialTheme.shapes.extraLarge,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.tertiaryContainer,
            contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
        ),
    ) {
        Column(Modifier.padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                ShapeBadge(Icons.Rounded.Lock, MaterialTheme.colorScheme.tertiary, MaterialTheme.colorScheme.onTertiary)
                Text("設定が保存されていません", style = MaterialTheme.typography.titleMedium)
            }
            Text(
                "Magisk でこのアプリの root 権限が「拒否」になっています。変更は今は効いていますが、" +
                    "再起動すると元に戻ります。Magisk の「スーパーユーザー」で Galaxy Downmixer を許可してから再試行してください。",
                style = MaterialTheme.typography.bodyMedium,
            )
            Button(onClick = onRetry, shapes = ButtonDefaults.shapes()) {
                Icon(Icons.Rounded.Refresh, contentDescription = null)
                Spacer(Modifier.width(ButtonDefaults.IconSpacing))
                Text("再試行")
            }
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun ShapeBadge(icon: androidx.compose.ui.graphics.vector.ImageVector, container: androidx.compose.ui.graphics.Color, content: androidx.compose.ui.graphics.Color) {
    Box(
        Modifier.size(48.dp).clip(MaterialShapes.Cookie9Sided.toShape()).background(container),
        contentAlignment = Alignment.Center,
    ) {
        Icon(icon, contentDescription = null, tint = content)
    }
}

/** Connected button group choosing between our downmix and Android's own (A/B). */
@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun ModeSelector(enabled: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween),
    ) {
        ToggleButton(
            checked = enabled,
            onCheckedChange = { onChange(true) },
            shapes = ButtonGroupDefaults.connectedLeadingButtonShapes(),
            modifier = Modifier.weight(1f).semantics { role = Role.RadioButton },
        ) {
            Icon(Icons.Rounded.SurroundSound, contentDescription = null)
            Spacer(Modifier.size(ToggleButtonDefaults.IconSpacing))
            Text("ダウンミックス")
        }
        ToggleButton(
            checked = !enabled,
            onCheckedChange = { onChange(false) },
            shapes = ButtonGroupDefaults.connectedTrailingButtonShapes(),
            modifier = Modifier.weight(1f).semantics { role = Role.RadioButton },
        ) {
            Icon(Icons.AutoMirrored.Rounded.CallMerge, contentDescription = null)
            Spacer(Modifier.size(ToggleButtonDefaults.IconSpacing))
            Text("Android 標準")
        }
    }
}

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
private fun InputCard(state: UiState, onToggleMute: (Int) -> Unit) {
    SectionCard(
        title = "入力チャンネル",
        trailing = {
            state.status?.takeIf { it.channelMask != 0 }?.let {
                Text(Channels.layoutName(it.channelMask), style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.primary)
            }
        },
    ) {
        if (state.inputs.isEmpty()) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                LoadingIndicator(Modifier.size(40.dp))
                Text("Apple Music で Dolby Atmos の曲を再生してください", style = MaterialTheme.typography.bodyMedium)
            }
        }
        state.inputs.forEach { row ->
            MeterRow(
                label = Channels.name(row.bit),
                level = row.level,
                dimmed = row.muted,
                trailing = {
                    FilledTonalIconToggleButton(
                        checked = row.muted,
                        onCheckedChange = { onToggleMute(row.bit) },
                        shapes = IconButtonDefaults.toggleableShapes(),
                    ) {
                        Icon(
                            if (row.muted) Icons.AutoMirrored.Rounded.VolumeOff else Icons.AutoMirrored.Rounded.VolumeUp,
                            contentDescription = if (row.muted) "${Channels.name(row.bit)} のミュートを解除" else "${Channels.name(row.bit)} をミュート",
                        )
                    }
                },
            )
        }
    }
}

@Composable
private fun OutputCard(state: UiState, onResetClip: () -> Unit) {
    SectionCard(
        title = "ダウンミックス後",
        trailing = {
            val clipped = state.clippedSamples > 0
            AssistChip(
                onClick = onResetClip,
                label = { Text(if (clipped) "クリップ ${state.clippedSamples}" else "クリップなし") },
                leadingIcon = if (clipped) { { Icon(Icons.Rounded.Warning, null, Modifier.size(AssistChipDefaults.IconSize)) } } else null,
                colors = if (clipped) AssistChipDefaults.assistChipColors(
                    containerColor = MaterialTheme.colorScheme.errorContainer,
                    labelColor = MaterialTheme.colorScheme.onErrorContainer,
                    leadingIconContentColor = MaterialTheme.colorScheme.onErrorContainer,
                ) else AssistChipDefaults.assistChipColors(),
            )
        },
    ) {
        Text("マトリクス直後", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        MeterRow("L", state.mix[0])
        MeterRow("R", state.mix[1])
        HorizontalDivider()
        Text("最終出力（PEQ・スワップ後）", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        MeterRow("L", state.out[0])
        MeterRow("R", state.out[1])
    }
}

@Composable
private fun MeterRow(label: String, level: MeterLevel, dimmed: Boolean = false, trailing: (@Composable () -> Unit)? = null) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(label, style = MaterialTheme.typography.labelLarge, fontFamily = FontFamily.Monospace, modifier = Modifier.width(40.dp))
        LevelMeter(level, Modifier.weight(1f), dimmed = dimmed)
        Text(
            formatDb(level.peakDb),
            style = MaterialTheme.typography.labelMedium,
            fontFamily = FontFamily.Monospace,
            textAlign = TextAlign.End,
            color = if (dimmed) MaterialTheme.colorScheme.outline else MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.width(48.dp),
        )
        trailing?.invoke()
    }
}

@Composable
private fun ProcessingCard(state: UiState, vm: DownmixViewModel) {
    SectionCard(title = "処理") {
        SwitchRow("L/R 入れ替え", "左右独立 PEQ は物理的な左右のまま", state.swap, vm::setSwap)
        SwitchRow("LFE ローパス", "Butterworth 4 次 · 125 Hz · 24 dB/oct", state.lfeLowPass, vm::setLfeLowPass)
        SwitchRow("共通 PEQ", "左右に同じ補正", state.commonPeq, vm::setCommonPeq)
        SwitchRow("左右独立 PEQ", "イヤホンの左右差の補正", state.earPeq, vm::setEarPeq)
        HorizontalDivider()
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("マスターゲイン", style = MaterialTheme.typography.bodyLarge, modifier = Modifier.weight(1f))
            Text(String.format(Locale.ROOT, "%+.1f dB", state.masterDb), fontFamily = FontFamily.Monospace)
        }
        Slider(
            value = state.masterDb,
            onValueChange = { vm.setMasterDb((it * 2).let(Math::round) / 2f) },
            valueRange = -20f..6f,
        )
    }
}

@Composable
private fun SwitchRow(title: String, detail: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            Text(detail, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Switch(checked = checked, onCheckedChange = onChange)
    }
}


@Composable
private fun RecordCard(state: UiState, onToggleRecord: () -> Unit) {
    SectionCard(
        title = "録音 (Pre-EQ キャプチャ)",
        trailing = {
            if (state.isRecording) {
                AssistChip(
                    onClick = {},
                    label = { Text("● REC", color = MaterialTheme.colorScheme.error) },
                    colors = AssistChipDefaults.assistChipColors(
                        containerColor = MaterialTheme.colorScheme.errorContainer,
                    ),
                )
            }
        },
    ) {
        Text(
            text = "マスターゲイン適用後・EQ処理前の音声を 32-bit Float WAV として端末に保存します（波形解析・比較用）。",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        if (state.isRecording) {
            val totalSec = state.recordDurationSeconds.toInt()
            val min = totalSec / 60
            val sec = totalSec % 60
            val timeStr = String.format(Locale.ROOT, "%02d:%02d", min, sec)

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Column {
                    Text(
                        text = "録音時間: $timeStr",
                        style = MaterialTheme.typography.titleLarge,
                        fontFamily = FontFamily.Monospace,
                        color = MaterialTheme.colorScheme.error,
                    )
                    Text(
                        text = "${state.recordedFrames} フレーム (ドロップ: ${state.recordDropped})",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Button(
                    onClick = onToggleRecord,
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                ) {
                    Text("■ 停止")
                }
            }
        } else {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Text(
                    text = "保存先: /sdcard/Music/GalaxyDownmixer/",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.weight(1f).padding(end = 8.dp),
                )
                Button(
                    onClick = onToggleRecord,
                ) {
                    Text("● 録音開始")
                }
            }
        }
    }
}
