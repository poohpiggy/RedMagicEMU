package app.aemu.core

import android.app.ActivityManager
import android.content.Context
import android.os.Process
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import app.aemu.ui.BootHistory
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.NoSuchFileException
import java.nio.file.attribute.BasicFileAttributes

/** Расположение файлов одного образа внутри песочницы приложения. */
class VmPaths(val ctx: Context, val id: String, private val directory: File? = null) {
    val dir: File = directory ?: File(ImageStore.imagesDir(ctx), id)
    /** дерево гостя: сюда смотрит qemu через -L */
    val root: File = File(dir, "root")
    /** рабочий каталог: сокеты хоста, журналы, копии свойств */
    val bin: File = File(dir, "run")
    val meta: File = File(dir, "image.json")
    val props: File get() = File(root, "dev/__properties__")
    val propsTemplate: File get() = File(dir, "props.base")
    val fb: File get() = File(root, "dev/graphics/fb0")
    val binderSock: File get() = File(bin, "binder.sock")
    val hwbinderSock: File get() = File(bin, "hwbinder.sock")
    val vndbinderSock: File get() = File(bin, "vndbinder.sock")
    val inputSock: File get() = File(bin, "input.sock")
    val frameSock: File get() = File(bin, "frame.sock")
    val glSock: File get() = File(root, "dev/socket/gl")
    val creds: File get() = File(bin, "creds")
    val owners: File get() = File(root, "dhd.owners")
    val snapshots: File get() = File(dir, "snapshots")
    fun socket(name: String) = File(root, "dev/socket/$name")
    fun log(name: String) = File(bin, "$name.log")

    fun nativeBin(name: String): File = File(ctx.applicationInfo.nativeLibraryDir, name)
}

object ImageStore {
    fun imagesDir(ctx: Context): File = File(ctx.filesDir, "images").apply { mkdirs() }

    fun list(ctx: Context): List<GuestImage> =
        imagesDir(ctx).listFiles()?.mapNotNull { d ->
            get(ctx, d.name)
        }?.sortedByDescending { it.createdAt } ?: emptyList()

    fun get(ctx: Context, id: String): GuestImage? {
        if (!InstancePathSafety.safeInstance(imagesDir(ctx), id)) return null
        if (app.aemu.importer.PendingImports.isPublishing(VmPaths(ctx, id).dir)) return null
        val f = VmPaths(ctx, id).meta
        return runCatching {
            val dir = File(imagesDir(ctx).canonicalFile, id)
            if (f.parentFile.canonicalFile != dir || f.canonicalFile != File(dir, "image.json")) null
            else if (f.isFile) GuestImage.fromJson(JSONObject(f.readText())).takeIf { it.id == id } else null
        }.getOrNull()
    }

    fun save(ctx: Context, img: GuestImage) {
        val images = imagesDir(ctx)
        app.aemu.importer.PendingImports.withImagesLock(images) {
            saveExisting(images, img)
        }
    }

    private fun saveExisting(images: File, img: GuestImage) {
        val dir = File(images, img.id)
        if (!InstancePathSafety.safeInstance(images, img.id) ||
            app.aemu.importer.PendingImports.isPublishing(dir) ||
            !java.nio.file.Files.isRegularFile(File(dir, "image.json").toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS))
            throw java.io.IOException("Cannot update missing or unfinished instance")
        saveAt(dir, img)
    }

    internal fun saveAt(dir: File, img: GuestImage) {
        if (!dir.isDirectory && !dir.mkdirs()) throw java.io.IOException("Cannot create instance directory")
        SafeMetadataStore.write(dir, img.toJson().toString(2).toByteArray(Charsets.UTF_8))
    }

    fun newId(ctx: Context): String {
        val abc = "abcdefghijkmnpqrstuvwxyz23456789"
        while (true) {
            val id = (1..6).map { abc.random() }.joinToString("")
            if (!File(imagesDir(ctx), id).exists()) return id
        }
    }

    fun resetData(ctx: Context, id: String) {
        val img = get(ctx, id) ?: throw IOException("Instance is unavailable")
        val instance = VmPaths(ctx, id).dir
        InstanceRuntimeLease.acquire(instance, id).use { lease ->
            InstanceAdmission.requireVerified(instance, img.fingerprint)
            requireGuestStopped(ctx)
            if (!lease.isValid()) throw IOException("Instance runtime lease changed")
            GuestDataReset.clear(instance) {
                FirstRingtoneInitStore.open(instance, id).use { it.createFresh() }
                BootHistory(ctx.filesDir).reset(id)
            }
        }
    }

    // A released lease also occurs on process death. Check for orphaned Guest writers
    // and for PIDs from older versions that did not acquire this lease.
    private fun requireGuestStopped(ctx: Context) {
        val uid = Process.myUid()
        val mine = Process.myPid()
        val pkg = ctx.packageName
        val hostProcesses = setOf(pkg, "$pkg:vulkan", "$pkg:binder", "$pkg:gl")
        val manager = ctx.getSystemService(ActivityManager::class.java)
        val hostPids = manager.runningAppProcesses.orEmpty()
            .filter { it.uid == uid && it.processName in hostProcesses }
            .mapTo(mutableSetOf(mine)) { it.pid }
        val proc = File("/proc").toPath()
        Files.newDirectoryStream(proc).use { entries ->
            val pids = entries.mapNotNull { it.fileName.toString().toIntOrNull() }.filter { it > 0 }
            if (mine !in pids) throw IOException("Cannot verify Guest processes")
            for (pid in pids) {
                if (pid in hostPids) continue
                try {
                    Os.kill(pid, 0)
                } catch (e: ErrnoException) {
                    if (e.errno == OsConstants.EPERM || e.errno == OsConstants.ESRCH) continue
                    throw IOException("Cannot verify Guest processes", e)
                }
                throw InstanceRuntimeLease.InUse()
            }
        }
    }

    /** Changes the stored image from its current state (not from a copy read earlier: the machine updates boot counters). */
    @Synchronized
    fun update(ctx: Context, id: String, f: (GuestImage) -> GuestImage) {
        val images = imagesDir(ctx)
        updateLocked(images, { get(ctx, id) }, { saveExisting(images, it) }, f)
    }

    internal fun <T> updateLocked(images: File, read: () -> T?, write: (T) -> Unit, transform: (T) -> T) {
        app.aemu.importer.PendingImports.withImagesLock(images) {
            read()?.let { write(transform(it)) }
        }
    }

    fun wipe(f: File) {
        val isLink = runCatching { android.system.OsConstants.S_ISLNK(Os.lstat(f.path).st_mode) }.getOrDefault(false)
        if (!isLink && f.isDirectory) { f.setWritable(true, true); f.listFiles()?.forEach { wipe(it) } }
        f.delete()
    }

    fun du(f: File): Long {
        var n = 0L
        val st = ArrayDeque<File>().apply { add(f) }
        while (st.isNotEmpty()) {
            val x = st.removeLast()
            val attrs = try {
                Files.readAttributes(x.toPath(), BasicFileAttributes::class.java, NOFOLLOW_LINKS)
            } catch (_: NoSuchFileException) {
                continue
            }
            if (attrs.isSymbolicLink) continue
            if (attrs.isDirectory) x.listFiles()?.forEach { st.add(it) } else n += attrs.size()
        }
        return n
    }
}
