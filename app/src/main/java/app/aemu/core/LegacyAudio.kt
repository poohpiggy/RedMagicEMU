package app.aemu.core

import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * The emulator's audio HAL for Android 2.x (native/audiohal/legacy_audio.c, a libaudio.so with the C++
 * AudioHardwareInterface of hardware_legacy): output to /dev/eac and the phone's microphone at any rate.
 *
 * Vendors extend the interface classes (HTC's AudioStreamOut has four methods after setVolume and three after
 * getRenderPosition), so the order of the virtual tables is taken from the firmware itself: the vtables of
 * AudioHardwareGeneric, AudioStreamOutGeneric and AudioStreamInGeneric (AOSP code every 2.x build carries, in
 * libaudio.so or libaudioflinger.so) are read slot by slot; a slot whose function has a name gives the method, an
 * unnamed one (an inline method of the header) is placed by the method order of AOSP's AudioHardwareInterface.h,
 * and a slot AOSP does not have is "-" (the vendors' base classes return 0 there; the HAL does the same).
 * The result goes to /system/etc/aemu_legacy_audio.map for the HAL.
 */
object LegacyAudio {
    private val AOSP = mapOf(
        "hw" to listOf("D1", "D0", "initCheck", "setVoiceVolume", "setMasterVolume", "setMode", "setMicMute", "getMicMute",
            "setParameters", "getParameters", "getInputBufferSize", "openOutputStream", "closeOutputStream",
            "openInputStream", "closeInputStream", "dumpState", "dump"),
        "out" to listOf("D1", "D0", "sampleRate", "bufferSize", "channels", "format", "latency", "setVolume", "write",
            "standby", "dump", "setParameters", "getParameters", "getRenderPosition"),
        "in" to listOf("D1", "D0", "sampleRate", "bufferSize", "channels", "format", "setGain", "read", "dump", "standby",
            "setParameters", "getParameters", "getInputFramesLost"))
    private val VTABLES = mapOf(
        "hw" to "_ZTVN7android20AudioHardwareGenericE",
        "out" to "_ZTVN7android21AudioStreamOutGenericE",
        "in" to "_ZTVN7android20AudioStreamInGenericE")
    /** classes whose function names count as methods of the interface (the Generic ones and their bases) */
    private val OWN = listOf("AudioHardwareGeneric", "AudioStreamOutGeneric", "AudioStreamInGeneric", "AudioHardwareBase",
        "AudioHardwareInterface", "AudioStreamOut", "AudioStreamIn")

    private class Sym(val name: String, val value: Long, val size: Int)
    private class Elf(val d: ByteArray) {
        val b: ByteBuffer = ByteBuffer.wrap(d).order(ByteOrder.LITTLE_ENDIAN)
        val syms = ArrayList<Sym>()
        val relSym = HashMap<Long, String>()      // R_ARM_ABS32 / GLOB_DAT at an address → its symbol
        val loads = ArrayList<LongArray>()        // vaddr, offset, filesz
        /** prelinked libraries carry absolute addresses: the base is in the "PRE " tag at the end */
        val base: Long = if (d.size > 8 && String(d, d.size - 4, 4, Charsets.US_ASCII) == "PRE ") b.getInt(d.size - 8).toLong() and 0xffffffffL else 0L

        init {
            val shoff = b.getInt(32); val shentsize = b.getShort(46).toInt() and 0xffff; val shnum = b.getShort(48).toInt() and 0xffff
            val phoff = b.getInt(28); val phentsize = b.getShort(42).toInt() and 0xffff; val phnum = b.getShort(44).toInt() and 0xffff
            for (p in 0 until phnum) {
                val ph = phoff + p * phentsize
                if (b.getInt(ph) == 1) loads += longArrayOf(u(ph + 8), u(ph + 4), u(ph + 16))
            }
            var dynsym = -1; var dynstr = -1
            for (i in 0 until shnum) {
                val sh = shoff + i * shentsize
                if (b.getInt(sh + 4) == 11) { dynsym = sh; dynstr = shoff + b.getInt(sh + 24) * shentsize }
            }
            require(dynsym >= 0)
            val symOff = b.getInt(dynsym + 16); val symSize = b.getInt(dynsym + 20); val strOff = b.getInt(dynstr + 16)
            val names = ArrayList<String>()
            var s = symOff
            while (s + 16 <= symOff + symSize) {
                val n = str(strOff + b.getInt(s))
                names += n
                syms += Sym(n, u(s + 4), b.getInt(s + 8))
                s += 16
            }
            for (i in 0 until shnum) {
                val sh = shoff + i * shentsize
                if (b.getInt(sh + 4) != 9) continue   // SHT_REL
                val off = b.getInt(sh + 16); val size = b.getInt(sh + 20)
                var r = off
                while (r + 8 <= off + size) {
                    val info = b.getInt(r + 4); val type = info and 0xff; val si = info ushr 8
                    if ((type == 2 || type == 21) && si in 1 until names.size) relSym[u(r)] = names[si]
                    r += 8
                }
            }
        }
        fun u(o: Int) = b.getInt(o).toLong() and 0xffffffffL
        fun str(o: Int): String { var e = o; while (e < d.size && d[e] != 0.toByte()) e++; return String(d, o, e - o, Charsets.US_ASCII) }
        fun word(va: Long): Long? {
            for (l in loads) if (va >= l[0] && va + 4 <= l[0] + l[2]) return u((l[1] + va - l[0]).toInt())
            return null
        }
    }

    /** "_ZN7android21AudioStreamOutGeneric5writeEPKvj" → class and method ("D0"/"D1" for destructors) */
    private fun method(m: String): Pair<String, String>? {
        var i = when { m.startsWith("_ZNK") -> 4; m.startsWith("_ZN") -> 3; else -> return null }
        val parts = ArrayList<String>()
        while (i < m.length && m[i].isDigit()) {
            var j = i; while (j < m.length && m[j].isDigit()) j++
            val n = m.substring(i, j).toInt(); if (j + n > m.length) return null
            parts += m.substring(j, j + n); i = j + n
        }
        if (m.startsWith("D", i) && i + 1 < m.length) parts += "D" + m[i + 1]
        return if (parts.size >= 3) parts[parts.size - 2] to parts.last() else null
    }

    private fun slots(e: Elf, vt: Sym): List<String?> {
        val byAddr = HashMap<Long, MutableList<String>>()
        for (s in e.syms) if (s.value != 0L) byAddr.getOrPut(s.value and 1L.inv()) { ArrayList() } += s.name
        val out = ArrayList<String?>()
        for (k in 2 until vt.size / 4) {          // offset-to-top and typeinfo first
            val at = vt.value + 4L * k
            val names = e.relSym[at]?.let { listOf(it) } ?: run {
                val w = e.word(at) ?: 0L
                byAddr[w and 1L.inv()] ?: (if (e.base != 0L) byAddr[(w - e.base) and 1L.inv()] else null) ?: emptyList()
            }
            out += names.mapNotNull { method(it) }.firstOrNull { it.first in OWN }?.second
        }
        return out
    }

    /** the firmware's slot order put onto AOSP's methods; "-" for slots AOSP does not have */
    private fun align(cls: String, raw: List<String?>): List<String> {
        val aosp = AOSP.getValue(cls)
        val res = ArrayList<String>()
        var e = 0
        for ((i, n) in raw.withIndex()) {
            if (n != null) {
                val k = aosp.indexOf(n)
                if (k >= 0) { e = k + 1; res += n } else res += "-"
                continue
            }
            val nextNamed = raw.drop(i + 1).firstOrNull { it != null && it in aosp }
            if (e < aosp.size && aosp[e] != nextNamed) res += aosp[e++] else res += "-"
        }
        return res
    }

    /** The map for the HAL, or null when the firmware's tables are not found. */
    fun map(libs: List<File>, log: (String) -> Unit): String? {
        val found = HashMap<String, List<String>>()
        for (f in libs) {
            if (!f.isFile) continue
            val e = runCatching { Elf(f.readBytes()) }.getOrNull() ?: continue
            for ((cls, vt) in VTABLES) {
                if (cls in found) continue
                val s = e.syms.firstOrNull { it.name == vt && it.size > 8 } ?: continue
                found[cls] = align(cls, slots(e, s))
            }
        }
        if (found.size != 3) { log("audio: the firmware's AudioHardwareGeneric tables not found (${found.keys})"); return null }
        // every table needs what the HAL is called through
        for ((cls, need) in mapOf("hw" to listOf("openOutputStream", "openInputStream", "getInputBufferSize"),
                "out" to listOf("write", "sampleRate"), "in" to listOf("read", "sampleRate")))
            if (!found.getValue(cls).containsAll(need)) { log("audio: $cls table incomplete: ${found[cls]}"); return null }
        return listOf("hw", "out", "in").joinToString("\n", postfix = "\n") { c -> "$c ${found.getValue(c).size} " + found.getValue(c).joinToString(" ") }
    }

    /** what the libraries in [libDir] take from the firmware's libaudio.so [orig] (ours provides only the factories) */
    fun importsFromLibaudio(libDir: File, orig: File): Set<String>? = runCatching {
        val exp = Elf(orig.readBytes()).syms.filter { it.value != 0L }.map { it.name }.toSet()
        val out = HashSet<String>()
        for (f in libDir.listFiles().orEmpty()) {
            if (!f.name.endsWith(".so") || f.name == "libaudio.so") continue
            val d = f.readBytes()
            if (!String(d, Charsets.ISO_8859_1).contains("libaudio.so\u0000")) continue
            val e = runCatching { Elf(d) }.getOrNull() ?: continue
            for (s in e.syms) if (s.value == 0L && s.name in exp) out += s.name
        }
        out
    }.getOrNull()
}
