package app.aemu.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import app.aemu.R
import app.aemu.core.GuestBatteryControl
import app.aemu.core.HostBatteryReading
import androidx.compose.ui.unit.dp

@Composable
internal fun BatteryModeFields(
    host: Boolean,
    onHostChange: (Boolean) -> Unit,
    level: Float,
    onLevelChange: (Float) -> Unit,
    source: Int,
    onSourceChange: (Int) -> Unit,
    hostReading: HostBatteryReading?,
    busy: Boolean,
    onReset: (() -> Unit)? = null,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
            SegmentedButton(
                selected = host,
                onClick = { onHostChange(true) },
                shape = SegmentedButtonDefaults.itemShape(0, 2),
                enabled = !busy,
            ) { Text(stringResource(R.string.battery_mode_host), maxLines = 1) }
            SegmentedButton(
                selected = !host,
                onClick = { onHostChange(false) },
                shape = SegmentedButtonDefaults.itemShape(1, 2),
                enabled = !busy,
            ) { Text(stringResource(R.string.battery_mode_custom), maxLines = 1) }
        }
        if (host) {
            Text(stringResource(R.string.battery_sync_sub), style = MaterialTheme.typography.bodyMedium)
            if (hostReading == null) {
                Text(
                    stringResource(R.string.battery_host_unavailable),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.battery_level), Modifier.weight(1f))
                    Text("${hostReading.level}% · ${sourceLabel(hostReading.plugged)}")
                }
            }
        } else {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.battery_level), Modifier.weight(1f))
                Text("${level.toInt()}%", style = MaterialTheme.typography.labelLarge)
            }
            Slider(level, onLevelChange, valueRange = 0f..100f, enabled = !busy)
            Text(stringResource(R.string.battery_power_source), style = MaterialTheme.typography.bodyMedium)
            SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                val sources = listOf(
                    GuestBatteryControl.NONE to R.string.battery_source_none,
                    GuestBatteryControl.AC to R.string.battery_source_ac,
                    GuestBatteryControl.USB to R.string.battery_source_usb,
                    GuestBatteryControl.WIRELESS to R.string.battery_source_wireless,
                )
                sources.forEachIndexed { index, (value, label) ->
                    SegmentedButton(
                        selected = source == value,
                        onClick = { onSourceChange(value) },
                        shape = SegmentedButtonDefaults.itemShape(index, sources.size),
                        enabled = !busy,
                    ) { Text(stringResource(label), maxLines = 1) }
                }
            }
            if (onReset != null) TextButton(enabled = !busy, onClick = onReset) {
                Text(stringResource(R.string.m_reset))
            }
        }
    }
}

@Composable
private fun sourceLabel(value: Int): String = stringResource(when {
    value and GuestBatteryControl.WIRELESS != 0 -> R.string.battery_source_wireless
    value and GuestBatteryControl.USB != 0 -> R.string.battery_source_usb
    value and GuestBatteryControl.AC != 0 -> R.string.battery_source_ac
    else -> R.string.battery_source_none
})
