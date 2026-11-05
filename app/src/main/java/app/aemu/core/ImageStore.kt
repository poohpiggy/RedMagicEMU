package app.aemu.core

import android.content.Context
import android.system.Os
import org.json.JSONObject
import java.io.File

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
            val paths = VmPaths(ctx, img.id)
            if (!InstancePathSafety.safeInstance(images, img.id) ||
                app.aemu.importer.PendingImports.isPublishing(paths.dir) ||
                !java.nio.file.Files.isRegularFile(paths.meta.toPath(), java.nio.file.LinkOption.NOFOLLOW_LINKS))
                throw java.io.IOException("Cannot update missing or unfinished instance")
            saveAt(paths.dir, img)
        }
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

    /** Deletion is unavailable until it is coordinated with the VM process. */
    @Suppress("UNUSED_PARAMETER")
    fun delete(ctx: Context, id: String): Nothing =
        throw IllegalStateException("Instance deletion is unavailable while VM coordination is incomplete")

    /** Reset is unavailable until it is coordinated with the VM process. */
    @Suppress("UNUSED_PARAMETER")
    fun resetData(ctx: Context, id: String): Nothing =
        throw IllegalStateException("Instance reset is unavailable while VM coordination is incomplete")

    /** Changes the stored image from its current state (not from a copy read earlier: the machine updates boot counters). */
    @Synchronized
    fun update(ctx: Context, id: String, f: (GuestImage) -> GuestImage) {
        get(ctx, id)?.let { save(ctx, f(it)) }
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
            val isLink = runCatching { android.system.OsConstants.S_ISLNK(Os.lstat(x.path).st_mode) }.getOrDefault(false)
            if (isLink) continue
            if (x.isDirectory) x.listFiles()?.forEach { st.add(it) } else n += x.length()
        }
        return n
    }
}
