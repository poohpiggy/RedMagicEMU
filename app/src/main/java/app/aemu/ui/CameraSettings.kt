package app.aemu.ui

import android.content.Context
import android.hardware.camera2.CameraCharacteristics
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Tab
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import app.aemu.R
import app.aemu.core.CameraProfile
import app.aemu.core.VmPaths

/** one camera being edited: its automatic list and the values shown */
private class CamEdit(ctx: Context, val cam: CameraProfile.Cam, api: Int) {
    val auto = CameraProfile.auto(CameraProfile.phoneCameras(ctx).firstOrNull { it.first == cam.host }?.second, api)
    val values = mutableStateMapOf<String, String>().apply { putAll(CameraProfile.effective(ctx, cam, api)) }
    val keys = mutableStateListOf<String>().apply { addAll(CameraProfile.effective(ctx, cam, api).keys) }
}

/**
 * ⋮ → Camera: everything the guest is told about its cameras and how the phone's cameras feed them
 * (CameraProfile). Per camera: which phone camera, facing, orientation, mirror; and every Camera.Parameters key:
 * change, remove, add. Saved to files/images/<id>/camera.json as edits over the automatic profile.
 */
@Composable
fun CameraSettings(ctx: Context, paths: VmPaths, api: Int, onClose: () -> Unit) {
    val supported = api in CameraProfile.API_RANGE
    val phone = remember { CameraProfile.phoneCameras(ctx) }
    val cfg = remember { CameraProfile.load(ctx, paths) }
    val cams = remember { mutableStateListOf<CamEdit>().apply { cfg.cams.forEach { add(CamEdit(ctx, it, api)) } } }
    var tab by remember { mutableIntStateOf(0) }
    var filter by remember { mutableStateOf("") }
    var adding by remember { mutableStateOf(false) }
    var confirmReset by remember { mutableStateOf(false) }

    fun label(id: String): String {
        val c = phone.firstOrNull { it.first == id }?.second ?: return id
        val face = when (c.get(CameraCharacteristics.LENS_FACING)) {
            CameraCharacteristics.LENS_FACING_BACK -> ctx.getString(R.string.cam_back)
            CameraCharacteristics.LENS_FACING_FRONT -> ctx.getString(R.string.cam_front)
            else -> ctx.getString(R.string.cam_external) }
        val px = c.get(CameraCharacteristics.SENSOR_INFO_PIXEL_ARRAY_SIZE)
        return "$id · $face" + (px?.let { " · %.1f MP".format(it.width * it.height / 1e6) } ?: "")
    }

    fun save() {
        cfg.cams.clear()
        cams.forEach { e ->
            val c = e.cam
            c.set.clear(); c.remove.clear()
            // the camera's auto list may have changed with its phone camera: edits are kept against the new one
            val auto = CameraProfile.auto(phone.firstOrNull { it.first == c.host }?.second, api)
            for (k in e.keys) { val v = e.values[k] ?: continue; if (auto[k] != v) c.set[k] = v }
            for (k in auto.keys) if (k !in e.keys) c.remove += k
            cfg.cams += c
        }
        cfg.auto = false
        CameraProfile.save(paths, cfg)
    }

    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.surface) {
            Column(Modifier.fillMaxSize().padding(16.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.m_camera), style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f))
                    IconButton(onClick = { confirmReset = true }) { Icon(Icons.Rounded.RestartAlt, stringResource(R.string.cam_reset)) }
                    IconButton(onClick = onClose) { Icon(Icons.Rounded.Close, stringResource(R.string.close)) }
                }
                if (!supported) Text(stringResource(R.string.cam_unsupported), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error)
                Text(stringResource(R.string.cam_hint), style = MaterialTheme.typography.bodySmall)
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    ScrollableTabRow(selectedTabIndex = tab.coerceIn(0, (cams.size - 1).coerceAtLeast(0)), modifier = Modifier.weight(1f), edgePadding = 0.dp) {
                        cams.forEachIndexed { i, e ->
                            Tab(selected = i == tab, onClick = { tab = i }, text = {
                                Text("$i · " + stringResource(if (e.cam.facing == 1) R.string.cam_front else R.string.cam_back))
                            })
                        }
                    }
                    IconButton(enabled = cams.size < 8 && phone.isNotEmpty(), onClick = {
                        val (id, c) = phone.first()
                        cams += CamEdit(ctx, CameraProfile.Cam(id, if (c.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_FRONT) 1 else 0,
                            c.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 90), api)
                        tab = cams.size - 1
                    }) { Icon(Icons.Rounded.Add, stringResource(R.string.cam_add_camera)) }
                }
                val e = cams.getOrNull(tab)
                if (e == null) { Text(stringResource(R.string.cam_none)) } else {
                    var hostMenu by remember(e) { mutableStateOf(false) }
                    var facing by remember(e) { mutableIntStateOf(e.cam.facing) }
                    var orient by remember(e) { mutableIntStateOf(e.cam.orientation) }
                    var mirror by remember(e) { mutableStateOf(e.cam.mirror) }
                    var host by remember(e) { mutableStateOf(e.cam.host) }
                    Spacer(Modifier.height(8.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.cam_phone_camera), Modifier.weight(1f))
                        TextButton(onClick = { hostMenu = true }) { Text(label(host)) }
                        DropdownMenu(hostMenu, { hostMenu = false }) {
                            phone.forEach { (id, _) -> DropdownMenuItem(text = { Text(label(id)) }, onClick = {
                                hostMenu = false; host = id; e.cam.host = id
                                // the new camera's own abilities; the user's edits stay on top
                                val fresh = CameraProfile.auto(phone.firstOrNull { it.first == id }?.second, api)
                                val edits = e.keys.filter { e.values[it] != e.auto[it] }.associateWith { e.values[it]!! }
                                e.values.clear(); e.values.putAll(fresh); e.values.putAll(edits)
                                e.keys.clear(); e.keys.addAll(e.values.keys)
                            }) }
                        }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        Text(stringResource(R.string.cam_facing), Modifier.weight(1f))
                        FilterChip(facing == 0, { facing = 0; e.cam.facing = 0 }, { Text(stringResource(R.string.cam_back)) })
                        FilterChip(facing == 1, { facing = 1; e.cam.facing = 1 }, { Text(stringResource(R.string.cam_front)) })
                    }
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        Text(stringResource(R.string.cam_orientation), Modifier.weight(1f))
                        for (o in listOf(0, 90, 180, 270)) FilterChip(orient == o, { orient = o; e.cam.orientation = o }, { Text("$o°") })
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.cam_mirror), Modifier.weight(1f))
                        Switch(mirror, { mirror = it; e.cam.mirror = it })
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        OutlinedTextField(filter, { filter = it }, label = { Text(stringResource(R.string.cam_search)) },
                            singleLine = true, modifier = Modifier.weight(1f))
                        IconButton(onClick = { adding = true }) { Icon(Icons.Rounded.Add, stringResource(R.string.cam_add_param)) }
                        IconButton(enabled = cams.size > 1, onClick = { cams.removeAt(tab); tab = (tab - 1).coerceAtLeast(0) }) {
                            Icon(Icons.Rounded.Delete, stringResource(R.string.cam_remove_camera))
                        }
                    }
                    HorizontalDivider(Modifier.padding(vertical = 6.dp))
                    LazyColumn(Modifier.weight(1f)) {
                        items(e.keys.filter { filter.isBlank() || it.contains(filter.trim(), true) || (e.values[it] ?: "").contains(filter.trim(), true) }, key = { it }) { k ->
                            val changed = e.auto[k] != e.values[k]
                            Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                                OutlinedTextField(e.values[k] ?: "", { v -> e.values[k] = v.replace(";", "").replace("\n", "") },
                                    label = { Text(k + if (changed) " *" else "", fontFamily = FontFamily.Monospace) },
                                    textStyle = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
                                    singleLine = true, modifier = Modifier.weight(1f))
                                IconButton(onClick = { e.keys.remove(k); e.values.remove(k) }) { Icon(Icons.Rounded.Close, stringResource(R.string.cam_remove_param)) }
                            }
                        }
                    }
                    if (adding) {
                        var nk by remember { mutableStateOf("") }
                        var nv by remember { mutableStateOf("") }
                        AlertDialog(onDismissRequest = { adding = false }, title = { Text(stringResource(R.string.cam_add_param)) },
                            text = { Column {
                                OutlinedTextField(nk, { nk = it.replace(";", "").replace("=", "").trim() }, label = { Text(stringResource(R.string.cam_key)) }, singleLine = true)
                                OutlinedTextField(nv, { nv = it.replace(";", "") }, label = { Text(stringResource(R.string.cam_value)) }, singleLine = true)
                            } },
                            confirmButton = { Button(enabled = nk.isNotEmpty(), onClick = {
                                if (nk !in e.keys) e.keys += nk
                                e.values[nk] = nv; adding = false
                            }) { Text(stringResource(R.string.cam_add)) } },
                            dismissButton = { OutlinedButton(onClick = { adding = false }) { Text(stringResource(R.string.close)) } })
                    }
                }
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.End, modifier = Modifier.fillMaxWidth()) {
                    OutlinedButton(onClick = onClose) { Text(stringResource(R.string.close)) }
                    Spacer(Modifier.width(8.dp))
                    Button(onClick = { save(); onClose() }) { Text(stringResource(R.string.cam_save)) }
                }
            }
        }
        if (confirmReset) AlertDialog(onDismissRequest = { confirmReset = false },
            title = { Text(stringResource(R.string.cam_reset)) }, text = { Text(stringResource(R.string.cam_reset_q)) },
            confirmButton = { Button(onClick = { CameraProfile.reset(paths); confirmReset = false; onClose() }) { Text(stringResource(R.string.cam_reset)) } },
            dismissButton = { OutlinedButton(onClick = { confirmReset = false }) { Text(stringResource(R.string.close)) } })
    }
}
