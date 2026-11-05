package app.aemu.importer

import java.io.ByteArrayInputStream
import java.io.IOException
import java.io.InputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * EROFS images (system, vendor, product... of firmwares built for Android 13 and later), read without mounting.
 *
 * Superblock at 1024; an inode is at meta_blkaddr * block + nid * 32 (compact, 32 bytes, or extended, 64), followed by
 * its extended attributes (skipped) and then what its data layout says:
 *   flat plain       whole blocks from raw_blkaddr
 *   flat inline      the same, with the last partial block stored right after the inode
 *   chunk based      a table of block addresses, one per chunk
 *   compressed       a map header and one index per logical cluster (8 bytes each in the full layout, 2 or 4 bytes
 *                    packed in the compact one). A HEAD index starts an extent at its cluster offset and names the
 *                    block its compressed data is in; the extent ends where the next one starts. The last extent may
 *                    be stored after the indexes (tail packing) or in the packed inode (fragments).
 * Compressed data is LZ4 (what Android's build uses); with the zero padding feature it is right aligned in its
 * blocks. Directories are blocks of 12-byte entries with the names behind them.
 */
class ErofsImage(private val src: RandomSource, private val cancelled: () -> Boolean = { false }) : TreeSource {
    private fun checkCancelled() { if (cancelled()) throw ImportCancelledException(); if (Thread.currentThread().isInterrupted) throw IOException("EROFS: cancelled") }
    private val blkBits: Int
    private val blk: Int
    private val metaBase: Long
    private val rootNid: Long
    private val packedNid: Long
    private val zeroPadding: Boolean
    private var packed: ByteArray? = null
    private var loadingPacked = false

    private class Inode(val mode: Int, val size: Long, val layout: Int, val u: Int, val body: Long)
    private class Extent(val lofs: Long, val type: Int, var pa: Long, var plen: Int) {
        var llen = 0L
        var fragment = false
    }

    init {
        val sb = le(1024, 128)
        if (sb.getInt(0) != MAGIC) throw IOException("not an EROFS image")
        blkBits = sb.get(12).toInt()
        if (blkBits < 9 || blkBits > 16) throw IOException("EROFS: block size 2^$blkBits")
        blk = 1 shl blkBits
        rootNid = (sb.getShort(14).toInt() and 0xffff).toLong()
        metaBase = (sb.getInt(40).toLong() and 0xffffffffL) shl blkBits
        val incompat = sb.getInt(80)
        zeroPadding = incompat and 1 != 0
        packedNid = if (incompat and 0x20 != 0) sb.getLong(96) else -1
        if (incompat and 0x8 != 0 && sb.getShort(86).toInt() != 0) throw IOException("EROFS: an image over several devices")
    }

    private fun bytes(pos: Long, n: Int): ByteArray {
        if (pos < 0 || n < 0 || pos > src.size || n.toLong() > src.size - pos)
            throw IOException("EROFS: read outside image")
        val b = ByteBuffer.allocate(n)
        src.read(pos, b)
        if (b.hasRemaining()) throw IOException("EROFS: truncated image")
        return b.array()
    }
    private fun le(pos: Long, n: Int): ByteBuffer = ByteBuffer.wrap(bytes(pos, n)).order(ByteOrder.LITTLE_ENDIAN)

    private fun inode(nid: Long): Inode {
        if (nid < 0 || nid > (Long.MAX_VALUE - metaBase) / 32) throw IOException("EROFS: invalid inode")
        val pos = metaBase + nid * 32
        val b = le(pos, 64)
        val format = b.getShort(0).toInt() and 0xffff
        val extended = format and 1 != 0
        val xattrCount = b.getShort(2).toInt() and 0xffff
        val xattrSize = if (xattrCount == 0) 0 else 12 + (xattrCount - 1) * 4
        val size = if (extended) b.getLong(8) else b.getInt(8).toLong() and 0xffffffffL
        if (size < 0) throw IOException("EROFS: invalid inode size")
        return Inode(b.getShort(4).toInt() and 0xffff, size, (format shr 1) and 7, b.getInt(if (extended) 16 else 16),
            pos + (if (extended) 64 else 32) + xattrSize)
    }

    override fun walk(visit: (path: String, isDir: Boolean, link: String?, mode: Int, size: Long, data: (() -> InputStream)?) -> Unit) {
        checkCancelled()
        walkDir(rootNid, "", 0, visit)
    }

    private fun walkDir(nid: Long, path: String, depth: Int,
                        visit: (String, Boolean, String?, Int, Long, (() -> InputStream)?) -> Unit) {
        checkCancelled()
        if (depth > 64) throw IOException("EROFS: directory nesting limit")
        val dir = inode(nid)
        if (dir.mode and 0xf000 != 0x4000 || dir.size > 16L * 1024 * 1024)
            throw IOException("EROFS: invalid or oversized directory")
        val data = content(dir).use { it.readBytes() }
        if (data.size.toLong() != dir.size) throw IOException("EROFS: truncated directory")
        val b = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        var at = 0
        while (at < data.size) {
            val end = minOf(at + blk, data.size)
            if (end - at < 12) throw IOException("EROFS: truncated directory entry")
            val firstName = b.getShort(at + 8).toInt() and 0xffff
            if (firstName < 12 || firstName % 12 != 0 || firstName > end - at)
                throw IOException("EROFS: invalid directory entry offsets")
            val count = firstName / 12
            for (i in 0 until count) {
                if (i and 255 == 0) checkCancelled()
                val e = at + i * 12
                val child = b.getLong(e)
                val from = at + (b.getShort(e + 8).toInt() and 0xffff)
                var to = if (i + 1 < count) at + (b.getShort(e + 20).toInt() and 0xffff) else end
                if (from < at + firstName || from >= end || to > end || to < from) throw IOException("EROFS: broken directory $path")
                while (to > from && data[to - 1].toInt() == 0) to--
                val name = String(data, from, to - from, Charsets.UTF_8)
                if (name == "." || name == "..") continue
                if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains('\u0000'))
                    throw IOException("EROFS: invalid directory name")
                val p = if (path.isEmpty()) name else "$path/$name"
                val ino = inode(child)
                when (ino.mode and 0xf000) {
                    0x4000 -> { visit(p, true, null, ino.mode, 0, null); walkDir(child, p, depth + 1, visit) }
                    0xa000 -> {
                        if (ino.size > 4096) throw IOException("EROFS: oversized symbolic link")
                        val link = content(ino).use { it.readBytes() }
                        if (link.size.toLong() != ino.size) throw IOException("EROFS: truncated symbolic link")
                        visit(p, false, String(link, Charsets.UTF_8), ino.mode, 0, null)
                    }
                    0x8000 -> visit(p, false, null, ino.mode, ino.size) { content(ino) }
                    else -> throw IOException("EROFS: unsupported inode type at $p")
                }
            }
            at += blk
        }
    }

    private fun content(ino: Inode): InputStream = when (ino.layout) {
        0 -> Ranges(listOf((ino.u.toLong() and 0xffffffffL shl blkBits) to ino.size))
        2 -> {
            val tail = ino.size and (blk - 1).toLong()
            Ranges(listOf((ino.u.toLong() and 0xffffffffL shl blkBits) to ino.size - tail, ino.body to tail))
        }
        4 -> chunks(ino)
        1, 3 -> compressed(ino)
        else -> throw IOException("EROFS: data layout ${ino.layout}")
    }

    /** byte ranges of the image one after another; a negative position is a hole */
    private inner class Ranges(private val parts: List<Pair<Long, Long>>) : InputStream() {
        private var part = 0
        private var done = 0L
        private val one = ByteArray(1)
        override fun read(): Int = if (read(one, 0, 1) < 0) -1 else one[0].toInt() and 0xff
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            checkCancelled()
            if (len == 0) return 0
            while (part < parts.size && done >= parts[part].second) { part++; done = 0 }
            if (part >= parts.size) return -1
            val (pos, size) = parts[part]
            val n = minOf(len.toLong(), size - done).toInt()
            if (pos < 0) java.util.Arrays.fill(b, off, off + n, 0) else {
                if (pos > src.size || done > src.size - pos || n.toLong() > src.size - pos - done)
                    throw IOException("EROFS: data outside image")
                val dst = ByteBuffer.wrap(b, off, n)
                src.read(pos + done, dst)
                if (dst.hasRemaining()) throw IOException("EROFS: truncated data")
            }
            done += n
            return n
        }
    }

    private fun chunks(ino: Inode): InputStream {
        val chunkBits = blkBits + (ino.u and 0x1f)
        val indexes = ino.u and 0x20 != 0
        val unit = if (indexes) 8 else 4
        if (chunkBits !in 9..46) throw IOException("EROFS: invalid chunk size")
        val chunkSize = 1L shl chunkBits
        val countLong = ino.size / chunkSize + if (ino.size % chunkSize == 0L) 0 else 1
        if (countLong > MAX_INDEX_BYTES / unit) throw IOException("EROFS: oversized chunk table")
        val count = countLong.toInt()
        val table = le((ino.body + unit - 1) / unit * unit, count * unit)
        val parts = ArrayList<Pair<Long, Long>>(count)
        for (i in 0 until count) {
            if (i and 1023 == 0) checkCancelled()
            val addr = table.getInt(i * unit + (if (indexes) 4 else 0))
            val len = minOf(1L shl chunkBits, ino.size - (i.toLong() shl chunkBits))
            parts.add((if (addr == -1) -1L else addr.toLong() and 0xffffffffL shl blkBits) to len)
        }
        return Ranges(parts)
    }

    // ---------------------------------------------------------------- compressed files

    private fun compressed(ino: Inode): InputStream {
        if (ino.size == 0L) return ByteArrayInputStream(ByteArray(0))
        val hdrPos = (ino.body + 7) and 7L.inv()
        val h = le(hdrPos, 8)
        val clusterBits = h.get(7).toInt() and 0xff
        if (clusterBits and 0x80 != 0) {     // the whole file is in the packed inode
            val off = h.getLong(0) and Long.MAX_VALUE
            val data = packedData()
            if (off > data.size || ino.size > data.size - off) throw IOException("EROFS: invalid packed range")
            return ByteArrayInputStream(data, off.toInt(), ino.size.toInt())
        }
        val advise = h.getShort(4).toInt() and 0xffff
        val algo = h.get(6).toInt() and 0xff
        val lcBits = blkBits + (clusterBits and 7)
        val clusterSize = 1L shl lcBits
        val totalLong = ino.size / clusterSize + if (ino.size % clusterSize == 0L) 0 else 1
        if (totalLong > (MAX_INDEX_BYTES - 64 - blk) / 8) throw IOException("EROFS: oversized compressed index")
        val total = totalLong.toInt()
        val bigPcluster = advise and 2 != 0
        val full = ino.layout == 1
        val ebase = hdrPos + 8 + (if (full) 8 else 0)
        // the whole index area (and the tail-packed data behind it) at once
        val areaAt = ebase and 31L.inv()
        val area = le(areaAt, (ebase - areaAt).toInt() + total * 8 + 64 + blk)

        val extents = ArrayList<Extent>()
        var cur: Extent? = null
        var curSized = false
        var idataOff = 0L
        // compact layout: 4-byte entries up to a 32-byte boundary, then 2-byte ones in packs of 16, then 4-byte ones
        var initial4 = ((32 - ebase % 32) / 4).toInt()
        if (initial4 == 8) initial4 = 0
        val count2 = if (advise and 1 != 0 && initial4 < total) (total - initial4) / 16 * 16 else 0
        for (lcn in 0 until total) {
            if (lcn and 1023 == 0) checkCancelled()
            val type: Int; val lo: Int; var pblk = 0L; var cblk = 0
            if (full) {
                val e = (ebase - areaAt).toInt() + lcn * 8
                type = area.getShort(e).toInt() and 3
                if (type == NONHEAD) {
                    val d0 = area.getShort(e + 4).toInt() and 0xffff
                    lo = d0
                    if (d0 and CBLKCNT != 0) cblk = d0 and CBLKCNT.inv()
                } else {
                    lo = area.getShort(e + 2).toInt() and 0x7fff
                    pblk = area.getInt(e + 4).toLong() and 0xffffffffL
                }
                idataOff = ebase + (lcn + 1) * 8L
            } else {
                var pos = ebase
                var l = lcn
                val shift: Int
                if (l < initial4) shift = 2
                else {
                    pos += initial4 * 4; l -= initial4
                    if (l < count2) shift = 1 else { pos += count2 * 2; l -= count2; shift = 2 }
                }
                pos += l.toLong() shl shift
                val vcnt = if (shift == 2) 2 else 16
                if ((shift == 2 && lcBits > 14) || (shift == 1 && lcBits > 12)) throw IOException("EROFS: compact index, cluster 2^$lcBits")
                val pack = vcnt shl shift
                val base = pos and (pack - 1).toLong().inv()
                val at = (base - areaAt).toInt()
                var i = ((pos - base) shr shift).toInt()
                val loBits = maxOf(lcBits, 12)
                val bits = (pack - 4) * 8 / vcnt
                fun dec(k: Int): Int {       // low loBits: the value, then two bits of type
                    val p = bits * k
                    return (area.getInt(at + p / 8) ushr (p and 7)) and ((1 shl (loBits + 2)) - 1)
                }
                val v = dec(i)
                type = v shr loBits
                lo = v and ((1 shl loBits) - 1)
                idataOff = base + pack
                if (type == NONHEAD) {
                    if (lo and CBLKCNT != 0) cblk = lo and CBLKCNT.inv()
                } else {
                    // the block of a HEAD: the pack's base address plus the blocks of the extents before it in the pack
                    var n = if (bigPcluster) 0L else 1L
                    while (i > 0) {
                        i--
                        val w = dec(i)
                        val t = w shr loBits
                        val d = w and ((1 shl loBits) - 1)
                        if (!bigPcluster) {
                            if (t == NONHEAD) i -= d
                            if (i >= 0) n++
                        } else if (t == NONHEAD) {
                            if (d and CBLKCNT != 0) { i--; n += d and CBLKCNT.inv() }
                            else { if (d <= 1) throw IOException("EROFS: broken compact index"); i -= d - 2 }
                        } else n++
                    }
                    pblk = (area.getInt(at + pack - 4).toLong() and 0xffffffffL) + n
                }
            }
            if (type == NONHEAD) {
                val c = cur
                if (cblk > 0 && c != null && !curSized) { c.plen = cblk shl blkBits; curSized = true }
                continue
            }
            val start = (lcn.toLong() shl lcBits) + lo
            // (mkfs of HyperOS leaves a head exactly at the end of the file: an extent of no bytes)
            if (start >= ino.size && cur != null) continue
            cur?.let { it.llen = start - it.lofs }
            cur = Extent(start, type, pblk shl blkBits, blk).also { extents.add(it) }
            curSized = false
        }
        val last = cur ?: throw IOException("EROFS: a compressed file without extents")
        last.llen = ino.size - last.lofs
        if (advise and 0x20 != 0) { last.fragment = true; last.pa = h.getInt(0).toLong() and 0xffffffffL }
        else if (advise and 0x8 != 0) { last.pa = idataOff; last.plen = h.getShort(2).toInt() and 0xffff }
        if (extents[0].lofs != 0L || extents.any { it.llen <= 0 || it.llen > MAX_EXTENT }) throw IOException("EROFS: broken extent map (size ${ino.size}, layout ${ino.layout}, advise $advise, algo $algo, cluster 2^$lcBits, h ${h.getInt(0)}/${h.getShort(2)}, extents ${extents.take(6).joinToString { "${it.lofs}+${it.llen}t${it.type}" }} .. ${extents.takeLast(3).joinToString { "${it.lofs}+${it.llen}t${it.type}" }})")
        val interlaced = advise and 0x10 != 0
        return object : InputStream() {
            private var next = 0
            private var buf = ByteArray(0)
            private var at = 0
            private val one = ByteArray(1)
            override fun read(): Int = if (read(one, 0, 1) < 0) -1 else one[0].toInt() and 0xff
            override fun read(b: ByteArray, off: Int, len: Int): Int {
                checkCancelled()
                while (at >= buf.size) {
                    if (next >= extents.size) return -1
                    buf = extent(extents[next++], algo, interlaced); at = 0
                }
                val n = minOf(len, buf.size - at)
                System.arraycopy(buf, at, b, off, n)
                at += n
                return n
            }
        }
    }

    private fun extent(e: Extent, algo: Int, interlaced: Boolean): ByteArray {
        val out = ByteArray(e.llen.toInt())
        if (e.fragment) {
            val data = packedData()
            if (e.pa < 0 || e.pa > data.size || out.size.toLong() > data.size - e.pa) throw IOException("EROFS: invalid fragment range")
            System.arraycopy(data, e.pa.toInt(), out, 0, out.size); return out
        }
        val raw = bytes(e.pa, e.plen)
        if (e.type == PLAIN) {
            if (out.size > raw.size) throw IOException("EROFS: an uncompressed extent longer than its blocks")
            if (interlaced) {
                val shift = (e.lofs and (blk - 1).toLong()).toInt()
                for (i in out.indices) out[i] = raw[(shift + i) % raw.size]
            } else System.arraycopy(raw, 0, out, 0, out.size)
            return out
        }
        val a = if (e.type == HEAD2) algo shr 4 else algo and 15
        var from = 0
        if (a == 2) {   // DEFLATE: raw stream, right aligned like LZ4 when zero padding is on
            if (zeroPadding) while (from < raw.size && raw[from].toInt() == 0) from++
            val inf = java.util.zip.Inflater(true)
            try {
                inf.setInput(raw, from, raw.size - from)
                var o = 0
                while (o < out.size && !inf.finished()) {
                    val n = inf.inflate(out, o, out.size - o)
                    if (n == 0) break
                    o += n
                }
                if (o < out.size) throw IOException("EROFS: DEFLATE data ends early")
            } finally { inf.end() }
            return out
        }
        if (a != 0) throw IOException("EROFS: compression ${if (a == 1) "LZMA" else if (a == 3) "Zstandard" else a.toString()} is not read yet (LZ4, LZ4HC and DEFLATE are)")
        if (zeroPadding) while (from < raw.size && raw[from].toInt() == 0) from++
        lz4(raw, from, out)
        return out
    }

    /** LZ4 block; stops when the output is full (an extent may take only the start of what its blocks hold) */
    private fun lz4(inp: ByteArray, from: Int, out: ByteArray) {
        var i = from
        var o = 0
        while (i < inp.size && o < out.size) {
            checkCancelled()
            val token = inp[i++].toInt() and 0xff
            var lit = token shr 4
            if (lit == 15) while (true) {
                if (i >= inp.size) throw IOException("EROFS: truncated LZ4 literal length")
                val v = inp[i++].toInt() and 0xff
                if (lit > Int.MAX_VALUE - v) throw IOException("EROFS: invalid LZ4 literal length")
                lit += v; if (v != 255) break
            }
            if (lit > inp.size - i) throw IOException("EROFS: truncated LZ4 literal")
            val nl = minOf(lit, out.size - o)
            System.arraycopy(inp, i, out, o, nl)
            i += lit; o += nl
            if (o >= out.size || i + 2 > inp.size) break
            val dist = (inp[i].toInt() and 0xff) or ((inp[i + 1].toInt() and 0xff) shl 8)
            i += 2
            var len = token and 15
            if (len == 15) while (true) {
                if (i >= inp.size) throw IOException("EROFS: truncated LZ4 match length")
                val v = inp[i++].toInt() and 0xff
                if (len > Int.MAX_VALUE - v - 4) throw IOException("EROFS: invalid LZ4 match length")
                len += v; if (v != 255) break
            }
            len += 4
            if (dist == 0 || dist > o) throw IOException("EROFS: broken LZ4 data")
            var s = o - dist
            val end = o + minOf(len, out.size - o)
            while (o < end) out[o++] = out[s++]
        }
        if (o < out.size) throw IOException("EROFS: LZ4 data ends early")
    }

    private fun packedData(): ByteArray {
        packed?.let { return it }
        if (packedNid < 0) throw IOException("EROFS: a fragment without the packed inode")
        if (loadingPacked) throw IOException("EROFS: recursive packed inode")
        loadingPacked = true
        try {
            val ino = inode(packedNid)
            if (ino.size > 512L shl 20) throw IOException("EROFS: packed inode of ${ino.size shr 20} MB")
            val result = content(ino).use { it.readBytes() }
            if (result.size.toLong() != ino.size) throw IOException("EROFS: truncated packed inode")
            packed = result
            return result
        } finally { loadingPacked = false }
    }

    companion object {
        private const val MAGIC = 0xE0F5E1E2.toInt()
        private const val PLAIN = 0
        private const val NONHEAD = 2
        private const val HEAD2 = 3
        private const val CBLKCNT = 0x800
        private const val MAX_EXTENT = 256L shl 20
        private const val MAX_INDEX_BYTES = 32 * 1024 * 1024
    }
}
