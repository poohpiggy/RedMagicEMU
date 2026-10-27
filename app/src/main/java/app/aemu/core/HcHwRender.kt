package app.aemu.core

import android.content.Context
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Hardware drawing (libhwui) for 3.x firmwares built without it (Google's SDK images).
 *
 * Honeycomb's own apps assume it: View.fastInvalidate() (framework.jar 3.2, disassembled) clears DRAWN and asks the
 * parent for a redraw only when mAttachInfo.mHardwareAccelerated, so in software drawing every later invalidate() of
 * that view is dropped. Launcher2 calls it from Workspace.onDraw whenever the wallpaper offset moves: the home screen
 * froze on the first swipe. Tablets of the same build had libhwui and a libandroid_runtime linked with it; such a set
 * of the same build (ro.build.id + ro.build.version.incremental) is in assets/engines/hc/<id>-<incremental>. It goes
 * in only when every symbol it imports is in the image's libraries and the new libandroid_runtime still exports
 * everything the old one did. The replaced files are kept in system/.aemu-parked/hwui.
 */
object HcHwRender {
    private const val DIR = "engines/hc"

    fun install(ctx: Context, root: File, api: Int, log: (String) -> Unit) {
        if (api !in 11..13) return
        val prop = File(root, "system/build.prop").takeIf { it.isFile } ?: return
        val p = PropArea.parseProps(prop.readText())
        val set = "$DIR/${p["ro.build.id"]}-${p["ro.build.version.incremental"]}"
        val names = EngineAssets.list(ctx, set).filter { it.endsWith(".so") }
        if (names.isEmpty()) return
        val lib = File(root, "system/lib")
        val parked = File(root, "system/.aemu-parked/hwui")
        val marker = File(parked, "installed")
        if (marker.isFile) {
            // already ours: only refresh files the asset set changed
            for (n in names) {
                val f = File(lib, n)
                if (!f.isFile || f.length() != EngineAssets.size(ctx, "$set/$n")) copy(ctx, "$set/$n", f)
            }
            return
        }
        if (File(lib, "libhwui.so").isFile) return // the firmware draws with its own
        val fresh = names.associateWith { n -> EngineAssets.open(ctx, "$set/$n").use { it.readBytes() } }
        val defs = HashSet<String>()
        lib.listFiles().orEmpty().filter { it.isFile && it.name.endsWith(".so") && it.name !in fresh }
            .forEach { f -> runCatching { dyn(f.readBytes())?.let { defs += it.first } } }
        val dyns = fresh.mapValues { (_, b) -> dyn(b) }
        if (dyns.values.any { it == null }) { log("hwui: $set: not ELF32"); return }
        dyns.values.forEach { defs += it!!.first }
        for ((n, d) in dyns) {
            val miss = d!!.second - defs
            if (miss.isNotEmpty()) { log("hwui: $set: $n needs ${miss.size} missing symbols (${miss.take(3)}), not installed"); return }
        }
        for (n in fresh.keys) {
            val old = File(lib, n).takeIf { it.isFile } ?: continue
            val lost = (dyn(old.readBytes())?.first ?: emptySet()) - dyns[n]!!.first
            if (lost.isNotEmpty()) { log("hwui: $set: $n would lose ${lost.size} exports (${lost.take(3)}), not installed"); return }
        }
        parked.mkdirs()
        for ((n, b) in fresh) {
            val f = File(lib, n)
            if (f.isFile) { val keep = File(parked, n); if (!keep.isFile) f.copyTo(keep) }
            f.delete(); f.writeBytes(b); f.setReadable(true, false)
        }
        marker.writeText(names.joinToString("\n"))
        log("hwui: hardware drawing libraries of $set installed (${names.joinToString()})")
    }

    private fun copy(ctx: Context, a: String, f: File) {
        f.delete()
        EngineAssets.open(ctx, a).use { i -> f.outputStream().use { o -> i.copyTo(o) } }
        f.setReadable(true, false)
    }

    /** ELF32 .dynsym: defined names, undefined non-weak names; null when not ELF32 */
    private fun dyn(d: ByteArray): Pair<Set<String>, Set<String>>? {
        if (d.size < 52 || d[0] != 0x7f.toByte() || d[1] != 'E'.code.toByte() || d[4] != 1.toByte()) return null
        val b = ByteBuffer.wrap(d).order(ByteOrder.LITTLE_ENDIAN)
        val shoff = b.getInt(32); val shentsize = b.getShort(46).toInt() and 0xffff; val shnum = b.getShort(48).toInt() and 0xffff
        if (shoff <= 0 || shoff.toLong() + shnum.toLong() * shentsize > d.size) return null
        val def = HashSet<String>(); val und = HashSet<String>()
        for (i in 0 until shnum) {
            val sh = shoff + i * shentsize
            if (b.getInt(sh + 4) != 11) continue // SHT_DYNSYM
            val symOff = b.getInt(sh + 16); val symSize = b.getInt(sh + 20)
            val strOff = b.getInt(shoff + b.getInt(sh + 24) * shentsize + 16)
            var s = symOff + 16 // entry 0 is null
            while (s + 16 <= symOff + symSize && s + 16 <= d.size) {
                val nameOff = b.getInt(s); val info = d[s + 12].toInt() and 0xff; val shndx = b.getShort(s + 14).toInt() and 0xffff
                var e = strOff + nameOff
                while (e < d.size && d[e] != 0.toByte()) e++
                val name = String(d, strOff + nameOff, e - strOff - nameOff, Charsets.US_ASCII)
                if (name.isNotEmpty()) {
                    if (shndx != 0) def += name
                    else if ((info shr 4) != 2) und += name // STB_WEAK may stay unresolved
                }
                s += 16
            }
        }
        return def to und
    }
}
