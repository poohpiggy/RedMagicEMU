package app.aemu.ui

import android.app.Application
import android.net.Uri
import android.provider.OpenableColumns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import app.aemu.core.GuestImage
import app.aemu.core.ImageStore
import app.aemu.core.VmSettings
import app.aemu.importer.Importer
import app.aemu.importer.PendingImports
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

data class ImportState(
    val active: Boolean = false,
    val file: String = "",
    val step: String = "",
    val progress: Float = -1f,
    val log: List<String> = emptyList(),
    val error: String? = null,
    val cancelled: Boolean = false,
    val done: GuestImage? = null,
)

class LibraryModel(app: Application) : AndroidViewModel(app) {
    private val _images = MutableStateFlow<List<GuestImage>?>(null)
    val images: StateFlow<List<GuestImage>?> = _images
    private val _import = MutableStateFlow(ImportState())
    val import: StateFlow<ImportState> = _import
    private val recoveryNotice = ImportRecoveryNotice(
        recover = {
            val images = ImageStore.imagesDir(getApplication())
            PendingImports.cleanAbandoned(images)
            PendingImports.interruptedNoticeToken(images)
        },
        acknowledge = { displayed ->
            val images = ImageStore.imagesDir(getApplication())
            PendingImports.acknowledgeInterrupted(images, displayed)
            PendingImports.interruptedNoticeToken(images)
        },
    )
    val importRecovery: StateFlow<ImportRecoveryNoticeState> = recoveryNotice.state
    private val cancellationLock = Any()
    private var importCancellation: ImportCancellation? = null

    init {
        refresh()
        viewModelScope.launch(Dispatchers.IO) {
            recoveryNotice.recover()
        }
    }

    fun refresh() {
        viewModelScope.launch {
            _images.value = withContext(Dispatchers.IO) { ImageStore.list(getApplication()) }
        }
    }

    fun import(uri: Uri) {
        if (_import.value.active) return
        val ctx = getApplication<Application>()
        val cancellation = ImportCancellation()
        synchronized(cancellationLock) { importCancellation = cancellation }
        _import.value = ImportState(active = true, file = "", step = "Starting")
        viewModelScope.launch(Dispatchers.IO) {
            val log = ArrayList<String>()
            val imp = Importer(ctx,
                onProgress = { step, p -> _import.value = _import.value.copy(step = step, progress = if (p >= 0) p else _import.value.progress.takeIf { it >= 0 } ?: -1f) },
                log = { s -> android.util.Log.i("aemu-import", s); log.add(s); _import.value = _import.value.copy(log = log.takeLast(50)) })
            try {
                val name = runCatching {
                    ctx.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                        if (c.moveToFirst()) c.getString(0) else null
                    }
                }.getOrNull() ?: uri.lastPathSegment?.substringAfterLast('/') ?: "firmware"
                _import.value = _import.value.copy(file = name)
                if (!cancellation.attach { imp.cancelled = true }) throw app.aemu.importer.ImportCancelledException()
                // умолчания из настроек приложения поверх того, что подобрал анализатор (экран, частоты)
                val raw = imp.import(uri, name)
                val d = app.aemu.AppPrefs.defaults(ctx)
                val img = raw.copy(settings = raw.settings.copy(gpu = d.gpu, jit = d.jit, netProxy = d.netProxy,
                    showNavBar = d.showNavBar, keepScreenOn = d.keepScreenOn, showLed = d.showLed))
                if (img != raw) ImageStore.save(ctx, img)
                _import.value = _import.value.copy(active = false, done = img, step = "Done", progress = 1f)
            } catch (t: Throwable) {
                _import.value = if (isUserCancellation(t)) _import.value.copy(active = false, cancelled = true)
                else _import.value.copy(active = false, error = t.message ?: t.toString())
            } finally {
                synchronized(cancellationLock) {
                    if (importCancellation === cancellation) importCancellation = null
                }
                recoveryNotice.recover()
                refresh()
            }
        }
    }

    fun cancelImport() { synchronized(cancellationLock) { importCancellation?.cancel() } }

    /** A machine is running (they live in the :vm process); its files must not be changed under it. */
    fun vmRunning(): Boolean {
        val ctx = getApplication<Application>()
        val am = ctx.getSystemService(android.app.ActivityManager::class.java)
        return am.runningAppProcesses.orEmpty().any { it.processName == ctx.packageName + ":vm" }
    }

    fun dismissImport() { _import.value = ImportState() }
    fun dismissInterruptedImport(displayed: String) {
        viewModelScope.launch(Dispatchers.IO) { recoveryNotice.acknowledge(displayed) }
    }
    fun retryImportRecovery() { viewModelScope.launch(Dispatchers.IO) { recoveryNotice.recover() } }

    fun rename(img: GuestImage, name: String) = update(img) { it.copy(name = name) }

    fun updateSettings(img: GuestImage, s: VmSettings) = update(img) { it.copy(settings = s) }

    private fun update(img: GuestImage, f: (GuestImage) -> GuestImage) {
        viewModelScope.launch(Dispatchers.IO) {
            ImageStore.update(getApplication(), img.id, f)
            refresh()
        }
    }
}
