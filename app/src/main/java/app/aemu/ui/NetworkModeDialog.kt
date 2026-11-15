package app.aemu.ui

import android.widget.Toast
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import app.aemu.R
import app.aemu.core.TargetNetworkMode
import app.aemu.core.TargetWifiArtifacts
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Network mode is saved separately from the instance settings form. */
@Composable
internal fun NetworkModeDialog(instanceId: String, onDone: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val savedMessage = stringResource(R.string.m_network_mode_saved)
    var selected by remember(instanceId) { mutableStateOf(TargetNetworkMode.read(context, instanceId)) }
    var busy by remember { mutableStateOf(false) }
    var failed by remember { mutableStateOf(false) }
    AlertDialog(onDismissRequest = { if (!busy) onDone() },
        title = { Text(stringResource(R.string.m_network_mode)) },
        text = {
            Column {
                Text(stringResource(if (TargetWifiArtifacts.RUNTIME_READY) R.string.m_network_mode_test_note
                    else R.string.m_network_mode_note), style = MaterialTheme.typography.bodySmall)
                for (mode in TargetNetworkMode.entries) {
                    val label = when (mode) {
                        TargetNetworkMode.ETHERNET -> R.string.m_network_ethernet
                        TargetNetworkMode.WIFI -> R.string.m_network_wifi
                        TargetNetworkMode.LTE -> R.string.m_network_lte
                    }
                    Row(Modifier.fillMaxWidth().clickable(enabled = !busy) { selected = mode },
                        verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = selected == mode, onClick = { selected = mode }, enabled = !busy)
                        Text(stringResource(label))
                    }
                }
                if (failed) Text(stringResource(R.string.m_network_mode_save_failed), color = MaterialTheme.colorScheme.error)
            }
        },
        confirmButton = { TextButton(enabled = !busy, onClick = {
            busy = true
            failed = false
            val chosen = selected
            scope.launch {
                val ok = withContext(Dispatchers.IO) {
                    try { TargetNetworkMode.write(context, instanceId, chosen) } catch (_: Exception) { false }
                }
                busy = false
                if (ok) {
                    Toast.makeText(context, savedMessage, Toast.LENGTH_SHORT).show()
                    onDone()
                } else failed = true
            }
        }) { Text(stringResource(android.R.string.ok)) } },
        dismissButton = { TextButton(enabled = !busy, onClick = onDone) { Text(stringResource(android.R.string.cancel)) } })
}
