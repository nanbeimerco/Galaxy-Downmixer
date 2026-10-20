package com.galaxydownmixer.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Bluetooth
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.ExpandLess
import androidx.compose.material.icons.rounded.ExpandMore
import androidx.compose.material.icons.rounded.FileOpen
import androidx.compose.material.icons.rounded.GraphicEq
import androidx.compose.material.icons.rounded.Headphones
import androidx.compose.material.icons.rounded.Headset
import androidx.compose.material.icons.rounded.LibraryMusic
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.automirrored.rounded.VolumeUp
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.Usb
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.AssistChipDefaults
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3ExpressiveApi
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.galaxydownmixer.data.ConnectedAudioDevice
import com.galaxydownmixer.data.DeviceType
import com.galaxydownmixer.data.EqFilter
import com.galaxydownmixer.data.EqPreset
import com.galaxydownmixer.data.EqSection
import com.galaxydownmixer.data.ParsedEqProfile
import java.util.Locale

@OptIn(ExperimentalMaterial3ExpressiveApi::class)
@Composable
fun EqCardView(
    state: UiState,
    onLoad: () -> Unit,
    onClear: () -> Unit,
    onSelectPreset: (String) -> Unit,
    onDeletePreset: (String) -> Unit,
    onRenamePreset: (String, String) -> Unit,
    onAssignDevice: (String?) -> Unit,
    onToggleAutoSwitch: (Boolean) -> Unit,
) {
    var showPresetMenu by remember { mutableStateOf(false) }
    var showManagerDialog by remember { mutableStateOf(false) }
    var showFilterDetails by rememberSaveable { mutableStateOf(false) }
    var showWarnings by rememberSaveable { mutableStateOf(false) }
    var showDeviceDialog by remember { mutableStateOf(false) }

    val activePreset = state.eqPresets.find { it.id == state.activePresetId }
        ?: state.eqPresets.find { it.name == state.eqName }
    val profile = state.parsedEqProfile ?: activePreset?.profile
    val currentDevice = state.connectedDevice
    val assignedPresetId = state.deviceAssignments[currentDevice.key]
    val assignedPreset = state.eqPresets.find { it.id == assignedPresetId }

    SectionCard(title = "EQ（Equalizer APO 形式）") {
        // Preset header row
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            ShapeBadge(
                Icons.Rounded.GraphicEq,
                MaterialTheme.colorScheme.primaryContainer,
                MaterialTheme.colorScheme.onPrimaryContainer,
            )

            Box(modifier = Modifier.weight(1f)) {
                Column(
                    modifier = Modifier
                        .clip(RoundedCornerShape(8.dp))
                        .clickable { showPresetMenu = true }
                        .padding(vertical = 4.dp, horizontal = 6.dp),
                ) {
                    Text(
                        text = activePreset?.name ?: state.eqName ?: "未設定",
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                    Text(
                        text = if (profile != null && !profile.isEmpty) {
                            "合計 ${profile.totalFilters} フィルター (タップして切替)"
                        } else {
                            "タップしてプリセットを選択"
                        },
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }

                DropdownMenu(
                    expanded = showPresetMenu,
                    onDismissRequest = { showPresetMenu = false },
                ) {
                    if (state.eqPresets.isEmpty()) {
                        DropdownMenuItem(
                            text = { Text("保存されたプリセットはありません") },
                            onClick = { showPresetMenu = false },
                        )
                    } else {
                        state.eqPresets.forEach { preset ->
                            val isSelected = preset.id == state.activePresetId
                            DropdownMenuItem(
                                text = {
                                    Row(
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                                    ) {
                                        Text(
                                            text = preset.name,
                                            fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                                            modifier = Modifier.weight(1f),
                                        )
                                        if (isSelected) {
                                            Icon(
                                                Icons.Rounded.Check,
                                                contentDescription = null,
                                                tint = MaterialTheme.colorScheme.primary,
                                                modifier = Modifier.size(18.dp),
                                            )
                                        }
                                    }
                                },
                                onClick = {
                                    showPresetMenu = false
                                    onSelectPreset(preset.id)
                                },
                            )
                        }
                    }
                }
            }

            IconButton(onClick = { showManagerDialog = true }) {
                Icon(
                    Icons.Rounded.LibraryMusic,
                    contentDescription = "プリセット管理",
                    tint = MaterialTheme.colorScheme.primary,
                )
            }
        }

        // Connected Device Bar
        ConnectedDeviceBanner(
            device = currentDevice,
            assignedPreset = assignedPreset,
            isCurrentPresetAssigned = assignedPresetId != null && assignedPresetId == state.activePresetId,
            autoSwitchEnabled = state.autoDeviceSwitch,
            onClick = { showDeviceDialog = true },
        )

        // Filter details expand button & content
        if (profile != null && !profile.isEmpty) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(8.dp))
                    .clickable { showFilterDetails = !showFilterDetails }
                    .padding(vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Icon(
                        Icons.Rounded.Tune,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp),
                        tint = MaterialTheme.colorScheme.primary,
                    )
                    Text(
                        text = "フィルター詳細 (${profile.totalFilters} 個)",
                        style = MaterialTheme.typography.titleSmall,
                        color = MaterialTheme.colorScheme.primary,
                    )
                }
                Icon(
                    if (showFilterDetails) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            AnimatedVisibility(
                visible = showFilterDetails,
                enter = fadeIn() + expandVertically(),
                exit = fadeOut() + shrinkVertically(),
            ) {
                EqFilterListView(profile = profile)
            }
        }

        // Warnings section
        if (state.eqWarnings.isNotEmpty()) {
            TextButton(onClick = { showWarnings = !showWarnings }) {
                Icon(Icons.Rounded.Warning, contentDescription = null)
                Spacer(Modifier.width(ButtonDefaults.IconSpacing))
                Text("読み飛ばした行 ${state.eqWarnings.size} 件")
            }
            AnimatedVisibility(showWarnings) {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    state.eqWarnings.forEach {
                        Text(it, style = MaterialTheme.typography.bodySmall, fontFamily = FontFamily.Monospace)
                    }
                }
            }
        }

        // Action buttons
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onLoad, shapes = ButtonDefaults.shapes()) {
                Icon(Icons.Rounded.FileOpen, contentDescription = null)
                Spacer(Modifier.width(ButtonDefaults.IconSpacing))
                Text("ファイルを読み込む")
            }
            OutlinedButton(
                onClick = onClear,
                shapes = ButtonDefaults.shapes(),
                enabled = state.activePresetId != null || !state.eq.isEmpty,
            ) {
                Text("解除")
            }
        }
    }

    // Dialogs
    if (showManagerDialog) {
        EqPresetManagerDialog(
            presets = state.eqPresets,
            activePresetId = state.activePresetId,
            onDismiss = { showManagerDialog = false },
            onSelect = { id ->
                showManagerDialog = false
                onSelectPreset(id)
            },
            onDelete = onDeletePreset,
            onRename = onRenamePreset,
        )
    }

    if (showDeviceDialog) {
        DeviceAssignmentDialog(
            device = currentDevice,
            presets = state.eqPresets,
            assignedPresetId = assignedPresetId,
            autoSwitchEnabled = state.autoDeviceSwitch,
            onDismiss = { showDeviceDialog = false },
            onAssign = onAssignDevice,
            onToggleAutoSwitch = onToggleAutoSwitch,
        )
    }
}

@Composable
private fun ConnectedDeviceBanner(
    device: ConnectedAudioDevice,
    assignedPreset: EqPreset?,
    isCurrentPresetAssigned: Boolean,
    autoSwitchEnabled: Boolean,
    onClick: () -> Unit,
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(12.dp))
            .clickable(onClick = onClick),
        color = MaterialTheme.colorScheme.surfaceContainerHigh,
        shape = RoundedCornerShape(12.dp),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            val icon: ImageVector = when (device.type) {
                DeviceType.BLUETOOTH -> Icons.Rounded.Bluetooth
                DeviceType.USB -> Icons.Rounded.Usb
                DeviceType.WIRED -> Icons.Rounded.Headphones
                DeviceType.SPEAKER -> Icons.AutoMirrored.Rounded.VolumeUp
                DeviceType.OTHER -> Icons.Rounded.Headset
            }

            Box(
                modifier = Modifier
                    .size(32.dp)
                    .background(MaterialTheme.colorScheme.secondaryContainer, CircleShape),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    icon,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp),
                    tint = MaterialTheme.colorScheme.onSecondaryContainer,
                )
            }

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = device.name,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = when {
                        assignedPreset != null -> "割り当て: ${assignedPreset.name} ${if (autoSwitchEnabled) "(自動切替ON)" else ""}"
                        else -> "未割り当て (タップして設定)"
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = if (assignedPreset != null) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            AssistChip(
                onClick = onClick,
                label = {
                    Text(
                        text = if (isCurrentPresetAssigned) "割り当て済" else if (assignedPreset != null) "変更" else "割り当て",
                        style = MaterialTheme.typography.labelSmall,
                    )
                },
                leadingIcon = {
                    Icon(
                        Icons.Rounded.Link,
                        contentDescription = null,
                        modifier = Modifier.size(14.dp),
                    )
                },
                colors = AssistChipDefaults.assistChipColors(
                    containerColor = if (isCurrentPresetAssigned) {
                        MaterialTheme.colorScheme.primaryContainer
                    } else {
                        MaterialTheme.colorScheme.surfaceContainerHighest
                    },
                ),
            )
        }
    }
}

@Composable
fun EqFilterListView(profile: ParsedEqProfile) {
    var selectedTab by rememberSaveable { mutableIntStateOf(0) }

    val sections = listOf(
        Triple("共通", profile.common, profile.common.filters.size),
        Triple("左 (L)", profile.left, profile.left.filters.size),
        Triple("右 (R)", profile.right, profile.right.filters.size),
    )

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        // Section selector chips
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            sections.forEachIndexed { index, (title, _, count) ->
                FilterChip(
                    selected = selectedTab == index,
                    onClick = { selectedTab = index },
                    label = { Text("$title ($count)") },
                )
            }
        }

        val (_, section, _) = sections[selectedTab]

        if (section.filters.isEmpty()) {
            Text(
                text = "フィルターはありません",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(vertical = 8.dp),
            )
        } else {
            if (section.preampDb != 0f) {
                Surface(
                    color = MaterialTheme.colorScheme.surfaceContainerHighest,
                    shape = RoundedCornerShape(8.dp),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Row(
                        modifier = Modifier.padding(horizontal = 12.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = "プリアンプ:",
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.Medium,
                        )
                        Spacer(Modifier.width(8.dp))
                        Text(
                            text = String.format(Locale.ROOT, "%+.1f dB", section.preampDb),
                            style = MaterialTheme.typography.bodyMedium,
                            fontFamily = FontFamily.Monospace,
                            fontWeight = FontWeight.Bold,
                            color = if (section.preampDb < 0f) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                        )
                    }
                }
            }

            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                section.filters.forEach { filter ->
                    FilterItemRow(filter)
                }
            }
        }
    }
}

@Composable
private fun FilterItemRow(filter: EqFilter) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceContainer,
        shape = RoundedCornerShape(8.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            // Type badge
            val badgeContainerColor = when (filter.type) {
                "PK" -> MaterialTheme.colorScheme.primaryContainer
                "LSC" -> MaterialTheme.colorScheme.tertiaryContainer
                "HSC" -> MaterialTheme.colorScheme.secondaryContainer
                else -> MaterialTheme.colorScheme.surfaceContainerHighest
            }
            val badgeTextColor = when (filter.type) {
                "PK" -> MaterialTheme.colorScheme.onPrimaryContainer
                "LSC" -> MaterialTheme.colorScheme.onTertiaryContainer
                "HSC" -> MaterialTheme.colorScheme.onSecondaryContainer
                else -> MaterialTheme.colorScheme.onSurfaceVariant
            }

            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(4.dp))
                    .background(badgeContainerColor)
                    .padding(horizontal = 6.dp, vertical = 2.dp),
            ) {
                Text(
                    text = filter.type,
                    style = MaterialTheme.typography.labelSmall,
                    fontWeight = FontWeight.Bold,
                    color = badgeTextColor,
                )
            }

            // Frequency
            val freqStr = if (filter.freqHz >= 1000f) {
                String.format(Locale.ROOT, "%.1f kHz", filter.freqHz / 1000f).replace(".0 ", " ")
            } else {
                String.format(Locale.ROOT, "%.0f Hz", filter.freqHz)
            }
            Text(
                text = freqStr,
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.Medium,
                modifier = Modifier.width(64.dp),
            )

            // Gain
            val gainColor = when {
                filter.gainDb > 0f -> MaterialTheme.colorScheme.primary
                filter.gainDb < 0f -> MaterialTheme.colorScheme.error
                else -> MaterialTheme.colorScheme.onSurfaceVariant
            }
            Text(
                text = String.format(Locale.ROOT, "%+.1f dB", filter.gainDb),
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
                fontWeight = FontWeight.SemiBold,
                color = gainColor,
                modifier = Modifier.width(72.dp),
            )

            // Q
            Text(
                text = String.format(Locale.ROOT, "Q %.2f", filter.q),
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
fun EqPresetManagerDialog(
    presets: List<EqPreset>,
    activePresetId: String?,
    onDismiss: () -> Unit,
    onSelect: (String) -> Unit,
    onDelete: (String) -> Unit,
    onRename: (String, String) -> Unit,
) {
    var presetToRename by remember { mutableStateOf<EqPreset?>(null) }
    var presetToDelete by remember { mutableStateOf<EqPreset?>(null) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Icon(Icons.Rounded.LibraryMusic, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                Text("EQ プリセット管理")
            }
        },
        text = {
            if (presets.isEmpty()) {
                Text("保存されたプリセットはありません")
            } else {
                LazyColumn(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    items(presets, key = { it.id }) { preset ->
                        val isSelected = preset.id == activePresetId
                        Card(
                            colors = CardDefaults.cardColors(
                                containerColor = if (isSelected) {
                                    MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.5f)
                                } else {
                                    MaterialTheme.colorScheme.surfaceContainerHigh
                                },
                            ),
                            shape = RoundedCornerShape(12.dp),
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clickable { onSelect(preset.id) }
                                    .padding(horizontal = 12.dp, vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = preset.name,
                                        style = MaterialTheme.typography.titleSmall,
                                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Medium,
                                        color = if (isSelected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
                                    )
                                    Text(
                                        text = "${preset.profile.totalFilters} フィルター",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    )
                                }

                                IconButton(onClick = { presetToRename = preset }) {
                                    Icon(
                                        Icons.Rounded.Edit,
                                        contentDescription = "名前変更",
                                        modifier = Modifier.size(18.dp),
                                    )
                                }

                                IconButton(onClick = { presetToDelete = preset }) {
                                    Icon(
                                        Icons.Rounded.Delete,
                                        contentDescription = "削除",
                                        tint = MaterialTheme.colorScheme.error,
                                        modifier = Modifier.size(18.dp),
                                    )
                                }
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("閉じる")
            }
        },
    )

    presetToRename?.let { preset ->
        var newName by remember { mutableStateOf(preset.name) }
        AlertDialog(
            onDismissRequest = { presetToRename = null },
            title = { Text("プリセット名の変更") },
            text = {
                OutlinedTextField(
                    value = newName,
                    onValueChange = { newName = it },
                    singleLine = true,
                    label = { Text("プリセット名") },
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                Button(
                    onClick = {
                        onRename(preset.id, newName)
                        presetToRename = null
                    },
                    enabled = newName.isNotBlank(),
                ) {
                    Text("保存")
                }
            },
            dismissButton = {
                TextButton(onClick = { presetToRename = null }) {
                    Text("キャンセル")
                }
            },
        )
    }

    presetToDelete?.let { preset ->
        AlertDialog(
            onDismissRequest = { presetToDelete = null },
            title = { Text("プリセットの削除") },
            text = { Text("プリセット「${preset.name}」を削除してもよろしいですか？") },
            confirmButton = {
                Button(
                    onClick = {
                        onDelete(preset.id)
                        presetToDelete = null
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                ) {
                    Text("削除")
                }
            },
            dismissButton = {
                TextButton(onClick = { presetToDelete = null }) {
                    Text("キャンセル")
                }
            },
        )
    }
}

@Composable
fun DeviceAssignmentDialog(
    device: ConnectedAudioDevice,
    presets: List<EqPreset>,
    assignedPresetId: String?,
    autoSwitchEnabled: Boolean,
    onDismiss: () -> Unit,
    onAssign: (String?) -> Unit,
    onToggleAutoSwitch: (Boolean) -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Icon(Icons.Rounded.Link, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                Text("接続機器のプリセット割り当て")
            }
        },
        text = {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text(
                    text = "対象機器: ${device.name}",
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text("接続時に自動切り替え", style = MaterialTheme.typography.bodyMedium)
                        Text(
                            "この機器が接続された時、割り当てたプリセットを自動適用します",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    Switch(checked = autoSwitchEnabled, onCheckedChange = onToggleAutoSwitch)
                }

                Text(
                    text = "割り当てるプリセットを選択:",
                    style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.primary,
                )

                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(200.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    item {
                        Surface(
                            shape = RoundedCornerShape(8.dp),
                            color = if (assignedPresetId == null) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceContainer,
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    onAssign(null)
                                    onDismiss()
                                },
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Text("なし（自動切替しない）", style = MaterialTheme.typography.bodyMedium, modifier = Modifier.weight(1f))
                                if (assignedPresetId == null) {
                                    Icon(Icons.Rounded.Check, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                                }
                            }
                        }
                    }

                    items(presets, key = { it.id }) { preset ->
                        val isAssigned = preset.id == assignedPresetId
                        Surface(
                            shape = RoundedCornerShape(8.dp),
                            color = if (isAssigned) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceContainer,
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    onAssign(preset.id)
                                    onDismiss()
                                },
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 12.dp, vertical = 10.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(preset.name, style = MaterialTheme.typography.bodyMedium, fontWeight = if (isAssigned) FontWeight.Bold else FontWeight.Normal)
                                    Text("${preset.profile.totalFilters} フィルター", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                }
                                if (isAssigned) {
                                    Icon(Icons.Rounded.Check, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                                }
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("閉じる")
            }
        },
    )
}
