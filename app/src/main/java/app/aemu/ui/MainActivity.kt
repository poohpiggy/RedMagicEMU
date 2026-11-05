package app.aemu.ui

import android.Manifest
import android.content.Intent
import android.os.Build
import android.os.Bundle
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
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
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
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
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
import app.aemu.update.AppUpdateManager
import app.aemu.update.ReleaseInfo
import app.aemu.update.UpdateState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import app.aemu.core.GuestImage
import app.aemu.R
import androidx.compose.ui.res.stringResource
import androidx.compose.material.icons.rounded.Settings
import android.content.Context

val ACTION_BOOT = BuildConfig.APPLICATION_ID + ".BOOT"

class MainActivity : ComponentActivity() {
    private val model: LibraryModel by viewModels()

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
        askPermissions()
        if (shouldHandleInitialIntent(restored = savedInstanceState != null)) handleView(intent)
        setContent { AemuTheme { Library(model) } }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleView(intent)
        if (styleOf(applicationContext) != style) recreate()
    }

    override fun onResume() {
        super.onResume()
        if (styleOf(applicationContext) != style) { recreate(); return }
        model.refresh()
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

    private fun askPermissions() {
        val want = ArrayList<String>()
        if (Build.VERSION.SDK_INT >= 33) want += Manifest.permission.POST_NOTIFICATIONS
        if (Build.VERSION.SDK_INT <= 32) want += Manifest.permission.WRITE_EXTERNAL_STORAGE
        val need = want.filter { checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED }
        if (need.isNotEmpty()) requestPermissions(need.toTypedArray(), 1)
    }
}

internal fun shouldHandleInitialIntent(restored: Boolean): Boolean = !restored

@Composable
fun Library(model: LibraryModel) {
    val images by model.images.collectAsState()
    val imp by model.import.collectAsState()
    val importRecovery by model.importRecovery.collectAsState()
    val ctx = LocalContext.current
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let { model.import(it) } }
    var settingsFor by remember { mutableStateOf<GuestImage?>(null) }
    val state = singleSystemState(images?.size ?: 0, imp.active, images != null, imp.done != null)

    Scaffold(topBar = {
        LargeFlexibleTopAppBar(title = { Text(stringResource(R.string.app_name)) }, actions = {
            IconButton(onClick = { ctx.startActivity(Intent(ctx, AppSettingsActivity::class.java)) }) {
                Icon(Icons.Rounded.Settings, stringResource(R.string.settings))
            }
        })
    }) { pad ->
        Column(Modifier.fillMaxSize().padding(pad).verticalScroll(rememberScrollState()).padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)) {
            StorageAccessCard()
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
            if (imp.active || imp.error != null || imp.cancelled || imp.done != null) {
                ImportCard(imp, onCancel = model::cancelImport, onDismiss = model::dismissImport, canDismiss = state != SingleSystemState.LOADING)
            }
            when (state) {
                SingleSystemState.EMPTY -> {
                    Text(stringResource(R.string.single_not_imported), style = MaterialTheme.typography.titleLarge)
                    Text(stringResource(R.string.single_target))
                    Button(onClick = { pick.launch(arrayOf("*/*")) }) { Text(stringResource(R.string.single_choose)) }
                }
                SingleSystemState.LOADING -> LoadingIndicator()
                SingleSystemState.IMPORTING -> Unit
                SingleSystemState.CONFLICT -> Text(stringResource(R.string.single_conflict), color = MaterialTheme.colorScheme.error)
                SingleSystemState.READY -> {
                    val img = images!!.single()
                    Text(stringResource(R.string.single_ready), style = MaterialTheme.typography.titleLarge)
                    Text(stringResource(R.string.single_target))
                    Button(onClick = { VmActivity.start(ctx, img.id) }) { Text(stringResource(R.string.start)) }
                    TextButton(onClick = { settingsFor = img }) { Text(stringResource(R.string.settings)) }
                }
            }
        }
    }
    settingsFor?.let { img ->
        SettingsSheet(img, onDismiss = { settingsFor = null }, onSave = { s -> model.updateSettings(img, s); settingsFor = null })
    }

}

/** Доступ ко всем файлам: общая папка гостя в «Внутренний накопитель/AEmulator». */
@Composable
private fun StorageAccessCard() {
    if (Build.VERSION.SDK_INT < 30) return
    val ctx = LocalContext.current
    var granted by remember { mutableStateOf(android.os.Environment.isExternalStorageManager()) }
    val life = androidx.lifecycle.compose.LocalLifecycleOwner.current
    androidx.compose.runtime.DisposableEffect(life) {
        val obs = androidx.lifecycle.LifecycleEventObserver { _, e ->
            if (e == androidx.lifecycle.Lifecycle.Event.ON_RESUME) granted = android.os.Environment.isExternalStorageManager()
        }
        life.lifecycle.addObserver(obs)
        onDispose { life.lifecycle.removeObserver(obs) }
    }
    if (granted) return
    Card(shape = MaterialTheme.shapes.large, colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.tertiaryContainer)) {
        Column(Modifier.padding(20.dp)) {
            Text(stringResource(R.string.storage_title), style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(4.dp))
            Text(stringResource(R.string.storage_text), style = MaterialTheme.typography.bodyMedium)
            Spacer(Modifier.height(12.dp))
            Button(onClick = {
                runCatching {
                    ctx.startActivity(Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        android.net.Uri.parse("package:${ctx.packageName}")))
                }.onFailure { ctx.startActivity(Intent(android.provider.Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)) }
            }) { Text(stringResource(R.string.allow)) }
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
private fun ImportCard(s: ImportState, onCancel: () -> Unit, onDismiss: () -> Unit, canDismiss: Boolean = true) {
    Card(
        colors = CardDefaults.cardColors(containerColor = if (s.error != null) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.secondaryContainer),
        shape = MaterialTheme.shapes.large,
    ) {
        Column(Modifier.padding(20.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (s.active) LoadingIndicator(Modifier.size(40.dp)) else Icon(if (s.error != null) Icons.Rounded.Warning else Icons.Rounded.Android, null)
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text(when { s.error != null -> stringResource(R.string.import_failed); s.active -> stringResource(R.string.import_running); s.cancelled -> stringResource(R.string.import_cancelled); else -> stringResource(R.string.import_done) },
                        style = MaterialTheme.typography.titleMedium)
                    Text(s.file, style = MaterialTheme.typography.bodySmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
            Spacer(Modifier.height(12.dp))
            when {
                s.error != null -> Text(s.error, style = MaterialTheme.typography.bodyMedium)
                s.active -> {
                    if (s.progress in 0f..1f) LinearWavyProgressIndicator(progress = { s.progress }, modifier = Modifier.fillMaxWidth())
                    else LinearWavyProgressIndicator(modifier = Modifier.fillMaxWidth())
                    Spacer(Modifier.height(8.dp))
                    Text(s.step, style = MaterialTheme.typography.bodySmall, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                s.done != null -> {
                    Text(stringResource(R.string.single_target), style = MaterialTheme.typography.bodyMedium)
                    visibleImportWarnings(s.done.warnings).forEach { Text("• $it", style = MaterialTheme.typography.bodySmall) }
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                if (s.active) TextButton(onClick = onCancel) { Text(stringResource(R.string.cancel)) } else TextButton(enabled = canDismiss, onClick = onDismiss) { Text(stringResource(R.string.ok)) }
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
