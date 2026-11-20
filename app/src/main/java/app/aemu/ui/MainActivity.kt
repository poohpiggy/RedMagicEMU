package app.aemu.ui

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.text.format.Formatter
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.ListItem
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Checkbox
import androidx.compose.material3.FilterChip
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.clickable
import androidx.compose.material.icons.rounded.FileOpen
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Android
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.SwapVert
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.AssistChip
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearWavyProgressIndicator
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import app.aemu.BuildConfig
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.DialogProperties
import androidx.core.os.BundleCompat
import app.aemu.core.GuestImage
import app.aemu.core.BatteryModeIntent
import app.aemu.R
import androidx.compose.ui.res.stringResource
import android.content.Context

val ACTION_BOOT = BuildConfig.APPLICATION_ID + ".BOOT"

class MainActivity : ComponentActivity() {
    private val model: LibraryModel by viewModels()
    private var storageGranted by mutableStateOf<Boolean?>(null)
    private var storageSettingsUnavailable by mutableStateOf(false)
    private var pendingStorageIntent: Intent? = null
    private var requestedNotifications = false
    private val storageSettings = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        completeStorageRequest()
    }
    private val legacyStorage = registerForActivityResult(ActivityResultContracts.RequestPermission()) {
        completeStorageRequest()
    }

    /** Language this activity was created with: a change means its string resources are stale and need context recreation. */
    private var style = ""
    private fun styleOf(c: Context) = app.aemu.AppPrefs.language(c)

    override fun attachBaseContext(base: Context) {
        style = styleOf(base)
        super.attachBaseContext(app.aemu.AppPrefs.wrap(base))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        pendingStorageIntent = if (shouldHandleInitialIntent(restored = savedInstanceState != null)) intent
            else savedInstanceState?.let { BundleCompat.getParcelable(it, PENDING_STORAGE_INTENT, Intent::class.java) }
        setContent {
            AemuTheme {
                if (storageGranted == true) {
                    Library(model)
                } else {
                    Surface(Modifier.fillMaxSize()) {
                        if (storageGranted == false) {
                            StorageAccessDialog(storageSettingsUnavailable, ::requestStorageAccess, ::finishAndRemoveTask)
                        }
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        pendingStorageIntent = intent
        if (styleOf(applicationContext) != style) recreate()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        outState.putParcelable(PENDING_STORAGE_INTENT, pendingStorageIntent)
        super.onSaveInstanceState(outState)
    }

    override fun onResume() {
        super.onResume()
        if (styleOf(applicationContext) != style) { recreate(); return }
        refreshStorageAccess()
    }

    private fun refreshStorageAccess() {
        if (isFinishing) return
        storageGranted = hasRequiredStorageAccess(this)
        if (storageGranted != true) return
        askForNotifications()
        val pending = pendingStorageIntent
        pendingStorageIntent = null
        handleView(pending)
        model.refresh()
    }

    private fun requestStorageAccess() {
        storageSettingsUnavailable = false
        if (Build.VERSION.SDK_INT < 30) {
            val permission = Manifest.permission.WRITE_EXTERNAL_STORAGE
            val prefs = getPreferences(MODE_PRIVATE)
            if (!prefs.getBoolean(LEGACY_STORAGE_REQUESTED, false) || shouldShowRequestPermissionRationale(permission)) {
                prefs.edit().putBoolean(LEGACY_STORAGE_REQUESTED, true).apply()
                legacyStorage.launch(permission)
                return
            }
        }
        storageSettingsUnavailable = !openStorageAccessSettings(this, storageSettings::launch)
    }

    private fun completeStorageRequest() {
        refreshStorageAccess()
        if (storageGranted == false) finishAndRemoveTask()
    }

    private fun handleView(i: Intent?) {
        if (i?.action == Intent.ACTION_VIEW) {
            val u = i.data
            if (u != null) {
                model.import(u)
            } else {
                val p = i.getStringExtra("path") ?: i.getStringExtra("file")
                if (p != null) model.import(android.net.Uri.fromFile(java.io.File(p)))
            }
        }
        val importPath = i?.getStringExtra("import")
        if (importPath != null) {
            model.import(android.net.Uri.fromFile(java.io.File(importPath)))
        }
        // Shortcuts and automation: am start -a <applicationId>.BOOT --es id <image>
        if (i?.action == ACTION_BOOT) i.getStringExtra("id")?.let { VmActivity.start(this, it, i.getBooleanExtra("recovery", false)) }
    }

    private fun askForNotifications() {
        if (Build.VERSION.SDK_INT < 33 || requestedNotifications) return
        requestedNotifications = true
        if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
    }

    private companion object {
        const val PENDING_STORAGE_INTENT = "pendingStorageIntent"
        const val LEGACY_STORAGE_REQUESTED = "legacyStorageRequested"
    }
}

internal fun shouldHandleInitialIntent(restored: Boolean): Boolean = !restored

@Composable
fun Library(model: LibraryModel) {
    val images by model.images.collectAsState()
    val imp by model.import.collectAsState()
    val reset by model.reset.collectAsState()
    val importRecovery by model.importRecovery.collectAsState()
    val ctx = LocalContext.current
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null && hasRequiredStorageAccess(ctx)) model.import(uri)
    }
    var settingsFor by remember { mutableStateOf<GuestImage?>(null) }
    var resetFor by remember { mutableStateOf<GuestImage?>(null) }
    val state = singleSystemState(images?.size ?: 0, imp.active, images != null, imp.completed)

    Scaffold(topBar = {
        TopAppBar(title = { Text(stringResource(R.string.app_name)) }, actions = {
            IconButton(onClick = { ctx.startActivity(Intent(ctx, AppSettingsActivity::class.java)) }) {
                Icon(Icons.Rounded.Info, stringResource(R.string.about_title))
            }
        })
    }) { pad ->
        BoxWithConstraints(
            Modifier.fillMaxSize().padding(pad),
            contentAlignment = Alignment.TopCenter,
        ) {
            val phoneHeight = (maxHeight * 0.638f).coerceIn(144.dp, 462.dp)
            Column(
                Modifier.widthIn(max = 520.dp).fillMaxWidth()
                    .verticalScroll(rememberScrollState()).heightIn(min = maxHeight).padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically),
            ) {
                importRecovery.token?.let { displayed ->
                    Card {
                        Column(Modifier.padding(20.dp)) {
                            Text(stringResource(R.string.import_interrupted), style = MaterialTheme.typography.titleMedium)
                            TextButton(onClick = { model.dismissInterruptedImport(displayed) }) { Text(stringResource(R.string.ok)) }
                        }
                    }
                }
                importRecovery.error?.let { error ->
                    Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer)) {
                        Column(Modifier.padding(20.dp)) {
                            Text(stringResource(R.string.import_recovery_error), style = MaterialTheme.typography.titleMedium)
                            Text(error)
                            TextButton(onClick = model::retryImportRecovery) { Text(stringResource(R.string.retry)) }
                        }
                    }
                }
                if (!imp.active && (imp.error != null || imp.cancelled)) {
                    ImportCard(imp, onDismiss = model::dismissImport, canDismiss = state != SingleSystemState.LOADING)
                }
                when (state) {
                    SingleSystemState.EMPTY -> HomeDevice(null, R.string.single_not_imported, phoneHeight) {
                        Button(onClick = { pick.launch(arrayOf("*/*")) }) { Text(stringResource(R.string.single_choose)) }
                    }
                    SingleSystemState.LOADING -> LoadingIndicator(Modifier.align(Alignment.CenterHorizontally))
                    SingleSystemState.IMPORTING -> HomeDevice(
                        null, null, phoneHeight,
                        screen = { HomeImportProgress(imp.progress, importScreenStep(imp.step)) },
                    ) {
                        TextButton(onClick = model::cancelImport) { Text(stringResource(R.string.cancel)) }
                    }
                    SingleSystemState.CONFLICT -> Text(stringResource(R.string.single_conflict), color = MaterialTheme.colorScheme.error)
                    SingleSystemState.READY -> {
                        val img = images!!.single()
                        val resetting = reset == ResetState.RUNNING
                        val actionPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp)
                        HomeDevice(img.id, R.string.single_ready, phoneHeight) {
                            Row(
                                Modifier.widthIn(max = 360.dp).fillMaxWidth(),
                                horizontalArrangement = Arrangement.spacedBy(12.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Button(
                                    onClick = { VmActivity.start(ctx, img.id) },
                                    modifier = Modifier.weight(1f).heightIn(min = 48.dp),
                                    enabled = !resetting,
                                    contentPadding = actionPadding,
                                ) {
                                    Text(stringResource(R.string.start), fontSize = 16.sp, textAlign = TextAlign.Center)
                                }
                                FilledTonalButton(
                                    onClick = { settingsFor = img },
                                    modifier = Modifier.weight(1f).heightIn(min = 48.dp),
                                    enabled = !resetting,
                                    contentPadding = actionPadding,
                                ) {
                                    Text(stringResource(R.string.instance_settings), fontSize = 16.sp, textAlign = TextAlign.Center)
                                }
                            }
                        }
                        reset?.let { status ->
                            val message = when (status) {
                                ResetState.RUNNING -> R.string.reset_in_progress
                                ResetState.DONE -> R.string.reset_done
                                ResetState.BUSY -> R.string.reset_running
                                ResetState.FAILED -> R.string.reset_failed
                            }
                            if (resetting) LinearWavyProgressIndicator(modifier = Modifier.fillMaxWidth())
                            Text(
                                stringResource(message),
                                modifier = Modifier.fillMaxWidth(),
                                textAlign = TextAlign.Center,
                                color = when (status) {
                                    ResetState.BUSY, ResetState.FAILED -> MaterialTheme.colorScheme.error
                                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                                },
                            )
                        }
                    }
                }
            }
        }
    }
    settingsFor?.let { img ->
        SettingsSheet(img, onDismiss = { settingsFor = null }, onReset = {
            settingsFor = null
            resetFor = img
        }, onSave = { s, customBattery ->
            model.updateSettings(img, s) {
                BatteryModeIntent.publish(ctx, s.batterySync, customBattery)
            }
        })
    }
    resetFor?.let { img ->
        AlertDialog(
            onDismissRequest = { resetFor = null },
            title = { Text(stringResource(R.string.reset_title, img.name)) },
            text = { Text(stringResource(R.string.reset_text)) },
            confirmButton = {
                Button(
                    onClick = { resetFor = null; model.resetData(img.id) },
                    colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
                ) { Text(stringResource(R.string.reset_data)) }
            },
            dismissButton = {
                TextButton(onClick = { resetFor = null }) { Text(stringResource(R.string.cancel)) }
            },
        )
    }

}

@Composable
private fun StorageAccessDialog(settingsUnavailable: Boolean, onAccept: () -> Unit, onDecline: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDecline,
        properties = DialogProperties(dismissOnClickOutside = false),
        title = { Text(stringResource(R.string.storage_title)) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(stringResource(R.string.storage_text))
                if (settingsUnavailable) {
                    Text(stringResource(R.string.storage_settings_unavailable), color = MaterialTheme.colorScheme.error)
                }
            }
        },
        confirmButton = { Button(onClick = onAccept) { Text(stringResource(R.string.storage_accept)) } },
        dismissButton = { TextButton(onClick = onDecline) { Text(stringResource(R.string.storage_decline)) } },
    )
}

internal fun hasRequiredStorageAccess(context: Context): Boolean =
    if (Build.VERSION.SDK_INT >= 30) Environment.isExternalStorageManager()
    else context.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED

private fun openStorageAccessSettings(context: Context, launch: (Intent) -> Unit): Boolean {
    val packageUri = android.net.Uri.parse("package:${context.packageName}")
    val settings = if (Build.VERSION.SDK_INT >= 30) {
        listOf(
            Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, packageUri),
            Intent(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION),
        )
    } else {
        listOf(Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, packageUri))
    }
    return settings.any { intent ->
        try {
            launch(intent)
            true
        } catch (_: android.content.ActivityNotFoundException) {
            false
        } catch (_: SecurityException) {
            false
        }
    }
}

@Composable
private fun EmptyState(onHelp: () -> Unit) {
    Column(Modifier.fillMaxSize().padding(32.dp), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
        Surface(shape = MaterialTheme.shapes.extraLarge, color = MaterialTheme.colorScheme.primaryContainer, modifier = Modifier.size(112.dp)) {
            Box(contentAlignment = Alignment.Center) { Icon(Icons.Rounded.Android, null, Modifier.size(56.dp), tint = MaterialTheme.colorScheme.onPrimaryContainer) }
        }
        Spacer(Modifier.height(24.dp))
        Text(stringResource(R.string.empty_title), style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.empty_text),
            style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = androidx.compose.ui.text.style.TextAlign.Center)
        Spacer(Modifier.height(16.dp))
        TextButton(onClick = onHelp) { Text(stringResource(R.string.empty_help)) }
    }
}

@Composable
private fun ImportCard(s: ImportState, onDismiss: () -> Unit, canDismiss: Boolean = true) {
    Card(
        colors = CardDefaults.cardColors(containerColor = if (s.error != null) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.secondaryContainer),
        shape = MaterialTheme.shapes.large,
    ) {
        Column(Modifier.padding(20.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(if (s.error != null) Icons.Rounded.Warning else Icons.Rounded.Android, null)
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(stringResource(if (s.error != null) R.string.import_failed else R.string.import_cancelled),
                        style = MaterialTheme.typography.titleMedium)
                    Text(s.file, style = MaterialTheme.typography.bodySmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
            s.error?.let {
                Spacer(Modifier.height(12.dp))
                Text(it, style = MaterialTheme.typography.bodyMedium)
            }
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                TextButton(enabled = canDismiss, onClick = onDismiss) { Text(stringResource(R.string.ok)) }
            }
        }
    }
}

@Composable
private fun HelpDialog(onDismiss: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.help_title)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(stringResource(R.string.help_text))
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.ok)) } },
    )
}
