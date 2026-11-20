package app.aemu.ui

import app.aemu.R
import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.FiniteAnimationSpec
import androidx.compose.animation.core.tween
import androidx.compose.ui.res.stringResource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ArrowDropDown
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.TextButton
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import app.aemu.core.CpuInfo
import app.aemu.core.BatteryModeIntent
import app.aemu.core.Engine
import app.aemu.core.GuestBatteryState
import app.aemu.core.GuestImage
import app.aemu.core.HostBatteryProtocol
import app.aemu.core.TargetNetworkMode
import app.aemu.core.TargetWifiArtifacts
import app.aemu.core.TargetVirtualWifiConfig
import app.aemu.core.VmSettings
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.roundToInt

internal val INSTANCE_RAM_CHOICES_MB = listOf(4096, 6144, 8192, 12288, 16384)

internal fun instanceRamChoiceMb(savedMb: Int): Int =
    INSTANCE_RAM_CHOICES_MB.minBy { abs(it.toLong() - savedMb) }

internal fun showWifiDetails(targetNetwork: Boolean, mode: TargetNetworkMode): Boolean =
    targetNetwork && mode == TargetNetworkMode.WIFI

internal fun wifiConfigForSave(targetWifi: Boolean, mode: TargetNetworkMode,
        draft: TargetVirtualWifiConfig, previous: TargetVirtualWifiConfig): TargetVirtualWifiConfig? = when {
    !targetWifi || draft.isValid() -> draft
    mode == TargetNetworkMode.ETHERNET && previous.isValid() -> previous
    else -> null
}

@Composable
internal fun SettingsSheet(
    img: GuestImage,
    onDismiss: () -> Unit,
    onReset: () -> Unit,
    onSave: (VmSettings, GuestBatteryState?) -> Unit,
) {
    var s by remember(img.id) {
        mutableStateOf(img.settings.copy(ramMb = instanceRamChoiceMb(img.settings.ramMb)))
    }
    val targetWifi = img.model == "NX809J" && img.api == 36 && img.abi == "arm64-v8a"
    val targetNetwork = TargetWifiArtifacts.applies(img.api, img.model, img.abi, img.effective().engine)
    val context = androidx.compose.ui.platform.LocalContext.current
    var hostBattery by remember(context) {
        mutableStateOf(readHostBattery(context.registerReceiver(
            null,
            android.content.IntentFilter(android.content.Intent.ACTION_BATTERY_CHANGED),
        )))
    }
    DisposableEffect(context) {
        val rx = object : android.content.BroadcastReceiver() {
            override fun onReceive(context: android.content.Context, intent: android.content.Intent) {
                readHostBattery(intent)?.let { hostBattery = it }
            }
        }
        androidx.core.content.ContextCompat.registerReceiver(
            context,
            rx,
            android.content.IntentFilter(android.content.Intent.ACTION_BATTERY_CHANGED),
            androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        onDispose { runCatching { context.unregisterReceiver(rx) } }
    }
    var customBatteryLevel by remember(img.id) { mutableStateOf(100f) }
    var customBatteryTouched by remember(img.id) { mutableStateOf(false) }
    var customBatterySource by remember(img.id) {
        mutableStateOf(app.aemu.core.GuestBatteryControl.AC)
    }
    DisposableEffect(context, img.id) {
        val rx = object : android.content.BroadcastReceiver() {
            override fun onReceive(context: android.content.Context, intent: android.content.Intent) {
                val state = BatteryModeIntent.readSnapshot(intent) ?: return
                if (customBatteryTouched) return
                customBatteryLevel = state.level.toFloat()
                customBatterySource = state.source
            }
        }
        androidx.core.content.ContextCompat.registerReceiver(
            context,
            rx,
            android.content.IntentFilter(BatteryModeIntent.snapshotAction),
            androidx.core.content.ContextCompat.RECEIVER_NOT_EXPORTED,
        )
        BatteryModeIntent.request(context)
        onDispose { runCatching { context.unregisterReceiver(rx) } }
    }
    val configuration = androidx.compose.ui.platform.LocalConfiguration.current
    val hostDisplay = remember(context, configuration) { readHostDisplay(context) }
    var networkMode by remember(img.id) { mutableStateOf(if (targetNetwork) TargetNetworkMode.read(context, img.id) else TargetNetworkMode.ETHERNET) }
    var showNetworkMode by remember(img.id) { mutableStateOf(false) }
    val state = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scope = rememberCoroutineScope()
    var saving by remember(img.id) { mutableStateOf(false) }
    val motion = MaterialTheme.motionScheme
    val sheetMotion = remember(motion) {
        object : MotionScheme by motion {
            override fun <T> defaultSpatialSpec(): FiniteAnimationSpec<T> = tween(450, easing = FastOutSlowInEasing)
            override fun <T> fastEffectsSpec(): FiniteAnimationSpec<T> = tween(450, easing = FastOutSlowInEasing)
        }
    }
    MaterialTheme(motionScheme = sheetMotion) {
    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = state) {
        MaterialTheme(motionScheme = motion) {
        Column(
            Modifier.verticalScroll(rememberScrollState(), overscrollEffect = null)
                .padding(horizontal = 20.dp)
                .navigationBarsPadding(),
        ) {
            Text(stringResource(R.string.instance_settings), style = MaterialTheme.typography.headlineSmall)
            Text(img.name, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(16.dp))

            Text(stringResource(R.string.vs_screen), style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            if (s.legacyEngine && img.api < 14) {
                Text(stringResource(R.string.vs_fixed_2x), style = MaterialTheme.typography.bodyMedium)
            } else {
                val stockDisplay = s.withNx809jDisplay()
                DisplayPresetSelector(R.string.vs_screen_stock,
                    DisplayPreset(stockDisplay.width, stockDisplay.height, stockDisplay.density), s) {
                    s = s.copy(width = it.width, height = it.height, density = it.density)
                }
                if (hostDisplay != null) {
                    DisplayPresetSelector(R.string.vs_screen_host, hostDisplay, s) {
                        s = s.copy(width = it.width, height = it.height, density = it.density)
                    }
                } else Text(stringResource(R.string.vs_screen_host_unavailable), style = MaterialTheme.typography.bodySmall)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                    NumField(stringResource(R.string.vs_width), s.width, Modifier.weight(1f)) { s = s.copy(width = it.coerceIn(240, 2160) and 0x7ffffffe) }
                    NumField(stringResource(R.string.vs_height), s.height, Modifier.weight(1f)) { s = s.copy(height = it.coerceIn(320, 3840) and 0x7ffffffe) }
                    NumField(stringResource(R.string.vs_dpi), s.density, Modifier.weight(1f)) { s = s.copy(density = it.coerceIn(96, 640)) }
                }
                Text(stringResource(R.string.vs_screen_hint), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 6.dp))
            }
            Spacer(Modifier.height(16.dp))

            Text(stringResource(R.string.vs_perf), style = MaterialTheme.typography.titleMedium)
            val ramIdx = INSTANCE_RAM_CHOICES_MB.indexOf(s.ramMb)
            ListItem(
                headlineContent = { Text(stringResource(R.string.vs_ram)) },
                supportingContent = {
                    Column {
                        Text("${s.ramMb / 1024} GB")
                        Slider(
                            value = ramIdx.toFloat(),
                            onValueChange = { s = s.copy(ramMb = INSTANCE_RAM_CHOICES_MB[it.roundToInt()]) },
                            valueRange = 0f..INSTANCE_RAM_CHOICES_MB.lastIndex.toFloat(),
                            steps = INSTANCE_RAM_CHOICES_MB.size - 2,
                        )
                    }
                },
                colors = ListItemDefaults.colors(containerColor = Color.Transparent),
            )
            Toggle(stringResource(R.string.vs_lowram), stringResource(R.string.vs_lowram_sub), s.lowRam) { s = s.copy(lowRam = it) }
            Toggle(stringResource(R.string.vs_vulkan), stringResource(R.string.vs_vulkan_sub), s.vulkan) {
                s = s.copy(vulkan = it)
            }
            Toggle(stringResource(R.string.vs_opencl), stringResource(R.string.vs_opencl_sub), s.openCl) {
                s = s.copy(openCl = it)
            }
            Toggle(stringResource(R.string.vs_vsync), stringResource(R.string.vs_vsync_sub), s.vsync) { s = s.copy(vsync = it) }
            Spacer(Modifier.height(8.dp))

            Text(stringResource(R.string.vs_controls), style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            Column(Modifier.padding(horizontal = 16.dp)) {
                Text(stringResource(R.string.m_battery), style = MaterialTheme.typography.bodyLarge)
                Spacer(Modifier.height(8.dp))
                BatteryModeFields(
                    host = s.batterySync,
                    onHostChange = { s = s.copy(batterySync = it) },
                    level = customBatteryLevel,
                    onLevelChange = { customBatteryTouched = true; customBatteryLevel = it },
                    source = customBatterySource,
                    onSourceChange = { customBatteryTouched = true; customBatterySource = it },
                    hostReading = hostBattery,
                    busy = false,
                )
            }
            Toggle(stringResource(R.string.sensor_bridge), stringResource(R.string.sensor_bridge_sub), s.accelerometerBridge) { s = s.copy(accelerometerBridge = it) }
            NavBarSetup(s) { s = it }

            if (targetWifi) {
                Spacer(Modifier.height(4.dp))
                if (targetNetwork) {
                    ListItem(
                        headlineContent = { Text(stringResource(R.string.m_network_mode)) },
                        supportingContent = {
                            Column {
                                Text(stringResource(when (networkMode) {
                                    TargetNetworkMode.ETHERNET -> R.string.m_network_ethernet
                                    TargetNetworkMode.WIFI -> R.string.m_network_wifi
                                    TargetNetworkMode.LTE -> R.string.m_network_lte
                                }))
                                Text(stringResource(R.string.vs_network_mode_separate))
                            }
                        },
                        trailingContent = { TextButton(onClick = { showNetworkMode = true }) { Text(stringResource(R.string.settings)) } },
                        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
                    )
                }
                if (showWifiDetails(targetNetwork, networkMode)) {
                    Column(Modifier.fillMaxWidth().padding(horizontal = 16.dp)) {
                        Text(stringResource(R.string.vs_virtual_wifi), style = MaterialTheme.typography.titleMedium)
                        Text(stringResource(if (TargetWifiArtifacts.RUNTIME_READY) R.string.vs_virtual_wifi_ready else R.string.vs_virtual_wifi_pending), style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant)
                        OutlinedTextField(value = s.virtualWifi.ssid,
                            onValueChange = { s = s.copy(virtualWifi = s.virtualWifi.copy(ssid = it)) },
                            label = { Text(stringResource(R.string.vs_virtual_wifi_ssid)) },
                            singleLine = true, modifier = Modifier.fillMaxWidth())
                        Text(stringResource(R.string.vs_virtual_wifi_signal, s.virtualWifi.rssiDbm),
                            style = MaterialTheme.typography.bodyMedium)
                        Slider(value = s.virtualWifi.rssiDbm.coerceIn(-100, -30).toFloat(),
                            onValueChange = { s = s.copy(virtualWifi = s.virtualWifi.copy(rssiDbm = it.toInt())) },
                            valueRange = -100f..-30f)
                        if (!s.virtualWifi.isValid()) Text(stringResource(R.string.vs_virtual_wifi_invalid),
                            color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
            Spacer(Modifier.height(16.dp))
            val wifiForSave = wifiConfigForSave(targetWifi, networkMode, s.virtualWifi, img.settings.virtualWifi)
            Button(onClick = {
                if (wifiForSave != null && !saving) {
                    saving = true
                    onSave(
                        s.copy(virtualWifi = wifiForSave),
                        if (s.batterySync) null else GuestBatteryState(customBatteryLevel.toInt(), customBatterySource),
                    )
                    scope.launch {
                        state.hide()
                        if (!state.isVisible) onDismiss()
                    }
                }
            }, enabled = wifiForSave != null && !saving,
                modifier = Modifier.fillMaxWidth().height(52.dp)) { Text(stringResource(R.string.save)) }
            TextButton(
                onClick = onReset,
                enabled = !saving,
                modifier = Modifier.fillMaxWidth(),
            ) { Text(stringResource(R.string.reset_data), color = MaterialTheme.colorScheme.error) }
            Spacer(Modifier.height(16.dp))
        }
    }
    }
    }
    if (showNetworkMode) NetworkModeDialog(img.id) {
        networkMode = TargetNetworkMode.read(context, img.id)
        showNetworkMode = false
    }
}

private fun readHostBattery(intent: android.content.Intent?): app.aemu.core.HostBatteryReading? = intent?.let {
    HostBatteryProtocol.reading(
        it.getIntExtra(android.os.BatteryManager.EXTRA_LEVEL, -1),
        it.getIntExtra(android.os.BatteryManager.EXTRA_STATUS, android.os.BatteryManager.BATTERY_STATUS_UNKNOWN),
        it.getIntExtra(android.os.BatteryManager.EXTRA_PLUGGED, -1),
    )
}

@Composable
private fun DisplayPresetSelector(label: Int, base: DisplayPreset, settings: VmSettings, select: (DisplayPreset) -> Unit) {
    val options = remember(base) { scaledDisplayPresets(base) }
    val selected = options.firstOrNull {
        it.width == settings.width && it.height == settings.height && it.density == settings.density
    }
    val shown = selected ?: base
    var expanded by remember(base) { mutableStateOf(false) }
    Box(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            FilterChip(selected = selected != null, onClick = { select(base) }, modifier = Modifier.weight(1f),
                label = { Text(stringResource(label, shown.width, shown.height, shown.density)) })
            IconButton(onClick = { expanded = true }) {
                Icon(Icons.Rounded.ArrowDropDown, contentDescription = stringResource(R.string.vs_screen_choose))
            }
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEachIndexed { index, option ->
                val percent = listOf(100, 75, 50)[index]
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.vs_screen_scaled, percent, option.width, option.height, option.density)) },
                    trailingIcon = { RadioButton(selected = selected == option, onClick = null) },
                    onClick = { select(option); expanded = false },
                )
            }
        }
    }
}

/** Which buttons the bar has (tap to add or remove; added ones go to the end) and how big it is. */
@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
private fun NavBarSetup(s: VmSettings, set: (VmSettings) -> Unit) {
    Toggle(
        title = stringResource(R.string.vs_nav),
        on = s.showNavBar,
        supportingContent = {
            Column {
                Text(stringResource(R.string.vs_nav_sub))
                if (s.showNavBar) {
                    val keys = s.navKeys.split(',').filter { it in VmSettings.NAV_ALL }
                    val visibleKeys = listOf("back", "home", "recents")
                    androidx.compose.foundation.layout.FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        for (k in keys.filter { it in visibleKeys } + visibleKeys.filter { it !in keys }) {
                            val on = k in keys
                            androidx.compose.material3.FilterChip(
                                selected = on,
                                label = { Text(navKeyLabel(k)) },
                                onClick = { set(s.copy(navKeys = (if (on) keys - k else keys + k).joinToString(","))) },
                            )
                        }
                    }
                }
            }
        },
    ) { set(s.copy(showNavBar = it)) }
    if (s.showNavBar) {
        Toggle(stringResource(R.string.vs_nav_compact), stringResource(R.string.vs_nav_compact_sub), s.navSize == 0) {
            set(s.copy(navSize = if (it) 0 else 1))
        }
    }
}

@Composable
internal fun navKeyLabel(k: String) = stringResource(when (k) {
    "back" -> R.string.back; "home" -> R.string.home; "recents" -> R.string.recents; "menu" -> R.string.menu
    "search" -> R.string.nav_search; "volup" -> R.string.vol_up; "voldown" -> R.string.vol_down
    "power" -> R.string.power; else -> R.string.nav_dpad
})

@Composable
internal fun Toggle(title: String, sub: String, on: Boolean, set: (Boolean) -> Unit) {
    Toggle(title, on, supportingContent = { Text(sub) }, set = set)
}

@Composable
private fun Toggle(title: String, on: Boolean, supportingContent: @Composable () -> Unit, set: (Boolean) -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = supportingContent,
        trailingContent = { Switch(checked = on, onCheckedChange = set) },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun NumField(label: String, value: Int, modifier: Modifier, set: (Int) -> Unit) {
    var text by remember(value) { mutableStateOf(value.toString()) }
    OutlinedTextField(value = text, onValueChange = { v -> text = v.filter(Char::isDigit).take(4); text.toIntOrNull()?.let(set) },
        label = { Text(label) }, singleLine = true, modifier = modifier,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
}
