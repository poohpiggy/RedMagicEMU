package app.aemu.core

import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Точечные правки 32-битных ARM-библиотек гостя по именам экспортируемых функций.
 *
 * Нужны для проверок «настоящего железа», которые на эмуляторе проваливаются заведомо и
 * при провале роняют процесс (MediaTek DRVB) — их обходим так же, как обошёл бы их любой
 * эмулятор: функция проверки сразу возвращает «успех».
 */
object ElfPatch {
    private const val ARM_RET0 = 0xE3A00000.toInt()   // mov r0, #0
    private const val ARM_BX_LR = 0xE12FFF1E.toInt()  // bx lr
    private const val THUMB_RET0 = 0x47702000         // movs r0, #0 ; bx lr

    /** Адрес → смещение в файле, для каждой функции из списка. null — не ELF32 ARM или символа нет. */
    fun symbols(f: File, names: Set<String>): Map<String, Long>? {
        val d = runCatching { f.readBytes() }.getOrNull() ?: return null
        if (d.size < 52 || d[0] != 0x7f.toByte() || d[1] != 'E'.code.toByte() || d[4] != 1.toByte()) return null
        val b = ByteBuffer.wrap(d).order(ByteOrder.LITTLE_ENDIAN)
        if (b.getShort(18).toInt() != 40) return null // EM_ARM
        val shoff = b.getInt(32); val shentsize = b.getShort(46).toInt() and 0xffff; val shnum = b.getShort(48).toInt() and 0xffff
        val phoff = b.getInt(28); val phentsize = b.getShort(42).toInt() and 0xffff; val phnum = b.getShort(44).toInt() and 0xffff
        if (shoff <= 0 || shoff + shnum * shentsize > d.size) return null
        val out = HashMap<String, Long>()
        for (i in 0 until shnum) {
            val sh = shoff + i * shentsize
            if (b.getInt(sh + 4) != 11) continue // SHT_DYNSYM
            val symOff = b.getInt(sh + 16); val symSize = b.getInt(sh + 20); val link = b.getInt(sh + 24)
            val strOff = b.getInt(shoff + link * shentsize + 16)
            var s = symOff
            while (s + 16 <= symOff + symSize && s + 16 <= d.size) {
                val nameOff = b.getInt(s); val value = b.getInt(s + 4).toLong() and 0xffffffffL
                val info = d[s + 12].toInt()
                if ((info and 0xf) == 2 && value != 0L) { // STT_FUNC
                    var e = strOff + nameOff
                    while (e < d.size && d[e] != 0.toByte()) e++
                    val name = String(d, strOff + nameOff, e - strOff - nameOff, Charsets.US_ASCII)
                    if (name in names) {
                        // виртуальный адрес → смещение в файле по заголовкам программы
                        val va = value and 0xfffffffeL
                        for (p in 0 until phnum) {
                            val ph = phoff + p * phentsize
                            if (b.getInt(ph) != 1) continue
                            val off = b.getInt(ph + 4).toLong() and 0xffffffffL
                            val vaddr = b.getInt(ph + 8).toLong() and 0xffffffffL
                            val filesz = b.getInt(ph + 16).toLong() and 0xffffffffL
                            if (va >= vaddr && va + 8 <= vaddr + filesz) {
                                out[name] = (off + va - vaddr) or ((value and 1L) shl 40) // бит 40 — Thumb
                            }
                        }
                    }
                }
                s += 16
            }
        }
        return out
    }

    /**
     * PLD/PLDW (immediate) with bits 15:12 not 1111. ARM ARM A8.8.127: those bits are "(1)" (should-be-one); a
     * Cortex-A executes the hint whatever they hold, qemu decodes them strictly and raises SIGILL. MediaTek's 4.2
     * bionic (Lenovo B6000 linker and libc memcpy: f5900000 "pldw [r0]") has them as 0000. In ARM-mode functions
     * named by [prefixes] (string routines, which is where such preload code is), each such word gets the bits set.
     * Returns the number of words changed.
     */
    fun fixPreloadHints(f: File, prefixes: List<String>): Int {
        val d = runCatching { f.readBytes() }.getOrNull() ?: return 0
        if (d.size < 52 || d[0] != 0x7f.toByte() || d[1] != 'E'.code.toByte() || d[4] != 1.toByte()) return 0
        val b = ByteBuffer.wrap(d).order(ByteOrder.LITTLE_ENDIAN)
        if (b.getShort(18).toInt() != 40) return 0
        val shoff = b.getInt(32); val shentsize = b.getShort(46).toInt() and 0xffff; val shnum = b.getShort(48).toInt() and 0xffff
        val phoff = b.getInt(28); val phentsize = b.getShort(42).toInt() and 0xffff; val phnum = b.getShort(44).toInt() and 0xffff
        if (shoff <= 0 || shoff + shnum * shentsize > d.size) return 0
        fun fileOff(va: Long, len: Long): Long {
            for (p in 0 until phnum) {
                val ph = phoff + p * phentsize
                if (b.getInt(ph) != 1) continue
                val off = b.getInt(ph + 4).toLong() and 0xffffffffL
                val vaddr = b.getInt(ph + 8).toLong() and 0xffffffffL
                val filesz = b.getInt(ph + 16).toLong() and 0xffffffffL
                if (va >= vaddr && va + len <= vaddr + filesz) return off + va - vaddr
            }
            return -1
        }
        class Sym(val value: Long, val size: Long, val name: String)
        val syms = ArrayList<Sym>()
        for (i in 0 until shnum) {
            val sh = shoff + i * shentsize
            val type = b.getInt(sh + 4)
            if (type != 11 && type != 2) continue // SHT_DYNSYM, SHT_SYMTAB
            val symOff = b.getInt(sh + 16); val symSize = b.getInt(sh + 20); val link = b.getInt(sh + 24)
            if (link < 0 || link >= shnum) continue
            val strOff = b.getInt(shoff + link * shentsize + 16)
            var s = symOff
            while (s + 16 <= symOff + symSize && s + 16 <= d.size) {
                val value = b.getInt(s + 4).toLong() and 0xffffffffL
                if ((d[s + 12].toInt() and 0xf) == 2 && value != 0L) {
                    val nameOff = strOff + b.getInt(s)
                    var e = nameOff
                    while (e < d.size && d[e] != 0.toByte()) e++
                    val name = if (nameOff in 0 until d.size) String(d, nameOff, e - nameOff, Charsets.US_ASCII) else ""
                    syms += Sym(value, b.getInt(s + 8).toLong() and 0xffffffffL, name)
                }
                s += 16
            }
        }
        // hand-written string routines often carry no size (memcpy.S of 4.2 bionic): up to the next function
        val starts = syms.map { it.value and 1L.inv() }.distinct().sorted()
        var n = 0
        val done = HashSet<Long>()
        for (sym in syms) {
            if ((sym.value and 3L) != 0L || sym.value in done || prefixes.none { sym.name.startsWith(it) }) continue
            val next = starts.firstOrNull { it > sym.value } ?: (sym.value + 4096)
            val size = if (sym.size in 8..65536) sym.size else minOf(next - sym.value, 4096L)
            val off = fileOff(sym.value, size)
            if (off < 0) continue
            done += sym.value
            var o = off.toInt()
            while (o + 4 <= off + size) {
                val w = b.getInt(o)
                if ((w and 0xff300000.toInt()) == 0xf5100000.toInt() && (w and 0xf000) != 0xf000) { b.putInt(o, w or 0xf000); n++ }
                o += 4
            }
        }
        if (n > 0) runCatching { f.writeBytes(d) }.onFailure { return 0 }
        return n
    }

    /** Сделать функции «return 0». Возвращает число изменённых (уже исправленные не считаются). */
    fun returnZero(f: File, names: Set<String>): Int {
        val syms = symbols(f, names) ?: return 0
        var n = 0
        RandomAccessFile(f, "rw").use { raf ->
            for ((_, v) in syms) {
                val thumb = (v shr 40) and 1L == 1L
                val off = v and 0xffffffffffL
                val want = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN).apply {
                    if (thumb) putInt(THUMB_RET0).putInt(THUMB_RET0) else putInt(ARM_RET0).putInt(ARM_BX_LR)
                }.array()
                val have = ByteArray(if (thumb) 4 else 8)
                raf.seek(off); raf.readFully(have)
                if (have.contentEquals(want.copyOf(have.size))) continue
                raf.seek(off); raf.write(want, 0, have.size)
                n++
            }
        }
        return n
    }
}
