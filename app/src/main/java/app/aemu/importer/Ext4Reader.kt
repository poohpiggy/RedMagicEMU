package app.aemu.importer

import java.io.IOException
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.SeekableByteChannel

/** Источник с произвольным доступом (обычный файл, sparse-образ, кусок архива). */
interface RandomSource {
    val size: Long
    /** Читает ровно dst.remaining() байт с позиции pos (за концом — нули). */
    fun read(pos: Long, dst: ByteBuffer)
}

class ChannelSource(private val ch: SeekableByteChannel, private val base: Long = 0, override val size: Long = ch.size() - base) : RandomSource {
    override fun read(pos: Long, dst: ByteBuffer) {
        var p = pos
        synchronized(ch) {
            while (dst.hasRemaining()) {
                if (p >= size) { while (dst.hasRemaining()) dst.put(0); return }
                ch.position(base + p)
                val lim = dst.limit()
                val can = minOf(dst.remaining().toLong(), size - p).toInt()
                dst.limit(dst.position() + can)
                val n = ch.read(dst)
                dst.limit(lim)
                if (n <= 0) { while (dst.hasRemaining()) dst.put(0); return }
                p += n
            }
        }
    }
}

/**
 * Читатель ext2/3/4 только на чтение: суперблок, группы (32/64 бита), inode любых размеров,
 * extent-деревья и классические косвенные блоки, каталоги (линейные и htree), символьные ссылки,
 * inline data. Этого хватает для system.img любых Android 2.x–6.x.
 */
class Ext4Reader(private val src: RandomSource, private val strict: Boolean = false, private val cancelled: () -> Boolean = { false }) {
    private val maxFileBytes = 1L shl 30
    private val maxEntries = 1_000_000
    private var inodeCount = 0
    private fun checkCancelled() { if (cancelled()) throw ImportCancelledException(); if (Thread.currentThread().isInterrupted) throw IOException("EXT4: cancelled") }
    private fun readAt(pos: Long, dst: ByteBuffer) {
        checkCancelled()
        if (strict && (pos < 0 || pos > src.size || dst.remaining().toLong() > src.size - pos)) throw IOException("EXT4: read outside image")
        src.read(pos, dst)
        if (strict && dst.hasRemaining()) throw IOException("EXT4: truncated read")
    }
    val blockSize: Int
    private val inodesPerGroup: Int
    private val inodeSize: Int
    private val descSize: Int
    private val groupCount: Int
    private val firstDataBlock: Long
    private val incompat: Int
    val volumeName: String

    data class Node(
        val ino: Int, val mode: Int, val uid: Int, val gid: Int, val size: Long,
        val flags: Int, val block: ByteArray, val mtime: Long,
    ) {
        val isDir get() = mode and 0xF000 == 0x4000
        val isFile get() = mode and 0xF000 == 0x8000
        val isLink get() = mode and 0xF000 == 0xA000
        val perm get() = mode and 0xFFF
    }

    init {
        val sb = buf(1024)
        readAt(1024, sb)
        sb.flip()
        if (sb.getShort(0x38).toInt() and 0xffff != 0xEF53) throw IOException("not ext2/3/4 (no 0xEF53 magic)")
        val inodesCount = sb.getInt(0x0)
        inodeCount = inodesCount
        val blocksLo = sb.getInt(0x4).toLong() and 0xffffffffL
        firstDataBlock = sb.getInt(0x14).toLong() and 0xffffffffL
        val blockShift = sb.getInt(0x18)
        if (strict && blockShift !in 0..6) throw IOException("EXT4: invalid block size")
        blockSize = 1024 shl blockShift
        val blocksPerGroup = sb.getInt(0x20)
        inodesPerGroup = sb.getInt(0x28)
        val rev = sb.getInt(0x4C)
        inodeSize = if (rev == 0) 128 else sb.getShort(0x58).toInt() and 0xffff
        incompat = sb.getInt(0x60)
        val is64 = incompat and 0x80 != 0
        descSize = if (is64) maxOf(32, sb.getShort(0xFE).toInt() and 0xffff) else 32
        val blocksHi = if (is64) sb.getInt(0x150).toLong() and 0xffffffffL else 0
        val blocks = blocksLo or (blocksHi shl 32)
        if (strict && (inodesCount <= 0 || blocksPerGroup <= 0 || inodesPerGroup <= 0 || blocks <= firstDataBlock ||
                blocks > src.size / blockSize || inodeSize !in 128..blockSize || inodeSize % 4 != 0 || descSize !in 32..blockSize))
            throw IOException("EXT4: invalid filesystem geometry")
        val blockGroups = (blocks - firstDataBlock + blocksPerGroup - 1) / blocksPerGroup
        val inodeGroups = (inodesCount.toLong() + inodesPerGroup - 1) / inodesPerGroup
        if (strict && (blockGroups > 1_000_000 || inodeGroups > 1_000_000)) throw IOException("EXT4: too many groups")
        groupCount = maxOf(blockGroups.toInt(), inodeGroups.toInt())
        val nameBytes = ByteArray(16).also { for (i in 0 until 16) it[i] = sb.get(0x78 + i) }
        volumeName = String(nameBytes).trimEnd('\u0000')
    }

    private fun buf(n: Int) = ByteBuffer.allocate(n).order(ByteOrder.LITTLE_ENDIAN)

    private fun readBlock(b: Long, n: Int = blockSize): ByteBuffer {
        val bb = buf(n)
        if (strict && (b < 0 || b > Long.MAX_VALUE / blockSize || n < 0 || n > blockSize)) throw IOException("EXT4: invalid block")
        readAt(b * blockSize, bb)
        bb.flip()
        return bb
    }

    private val inodeTableCache = HashMap<Int, Long>()

    private fun inodeTable(group: Int): Long = inodeTableCache.getOrPut(group) {
        if (strict && group !in 0 until groupCount) throw IOException("EXT4: invalid inode group")
        val gdtBlock = firstDataBlock + 1
        val off = gdtBlock * blockSize + group.toLong() * descSize
        val d = buf(descSize)
        readAt(off, d); d.flip()
        val lo = d.getInt(0x8).toLong() and 0xffffffffL
        val hi = if (descSize >= 64) d.getInt(0x28).toLong() and 0xffffffffL else 0
        val table = lo or (hi shl 32)
        if (strict && (table <= 0 || table >= src.size / blockSize)) throw IOException("EXT4: inode table outside image")
        table
    }

    fun inode(ino: Int): Node {
        checkCancelled()
        if (strict && (ino <= 0 || ino > inodeCount)) throw IOException("EXT4: invalid inode number")
        val g = (ino - 1) / inodesPerGroup
        val idx = (ino - 1) % inodesPerGroup
        val off = inodeTable(g) * blockSize + idx.toLong() * inodeSize
        val b = buf(minOf(inodeSize, 256).coerceAtLeast(128))
        readAt(off, b); b.flip()
        val mode = b.getShort(0).toInt() and 0xffff
        val uidLo = b.getShort(2).toInt() and 0xffff
        val sizeLo = b.getInt(4).toLong() and 0xffffffffL
        val mtime = b.getInt(0x10).toLong() and 0xffffffffL
        val gidLo = b.getShort(0x18).toInt() and 0xffff
        val flags = b.getInt(0x20)
        val block = ByteArray(60).also { for (i in 0 until 60) it[i] = b.get(0x28 + i) }
        val sizeHi = b.getInt(0x6C).toLong() and 0xffffffffL
        val uidHi = b.getShort(0x78).toInt() and 0xffff
        val gidHi = b.getShort(0x7A).toInt() and 0xffff
        val size = sizeLo or (sizeHi shl 32)
        if (strict && (size < 0 || size > maxFileBytes)) throw IOException("EXT4: oversized inode")
        return Node(ino, mode, uidLo or (uidHi shl 16), gidLo or (gidHi shl 16), size, flags, block, mtime)
    }

    /** Список физических отрезков файла: (логический блок, физический блок, длина). */
    private fun extents(n: Node): List<LongArray> {
        val out = ArrayList<LongArray>()
        if (n.flags and 0x80000 != 0) { // EXT4_EXTENTS_FL
            walkExtents(ByteBuffer.wrap(n.block).order(ByteOrder.LITTLE_ENDIAN), out, 0, HashSet())
        } else {
            val bb = ByteBuffer.wrap(n.block).order(ByteOrder.LITTLE_ENDIAN)
            val nblocks = (n.size + blockSize - 1) / blockSize
            var logical = 0L
            val per = blockSize / 4
            fun add(phys: Long) {
                if (logical >= nblocks) return
                if (phys != 0L) {
                    val last = out.lastOrNull()
                    if (last != null && last[0] + last[2] == logical && last[1] + last[2] == phys) last[2]++
                    else out.add(longArrayOf(logical, phys, 1))
                }
                logical++
            }
            fun indirect(blk: Long, depth: Int) {
                if (logical >= nblocks) return
                if (blk == 0L) { // дыра: пропустить все блоки этого поддерева
                    var span = 1L; repeat(depth) { span *= per }
                    logical += span; return
                }
                val ib = readBlock(blk)
                for (i in 0 until per) {
                    if (logical >= nblocks) return
                    val p = ib.getInt(i * 4).toLong() and 0xffffffffL
                    if (depth == 1) add(p) else indirect(p, depth - 1)
                }
            }
            for (i in 0 until 12) add(bb.getInt(i * 4).toLong() and 0xffffffffL)
            indirect(bb.getInt(48).toLong() and 0xffffffffL, 1)
            indirect(bb.getInt(52).toLong() and 0xffffffffL, 2)
            indirect(bb.getInt(56).toLong() and 0xffffffffL, 3)
        }
        return out
    }

    private fun walkExtents(h: ByteBuffer, out: MutableList<LongArray>, level: Int, visited: MutableSet<Long>) {
        checkCancelled()
        if (level > 8) { if (strict) throw IOException("EXT4: extent tree too deep") else return }
        val magic = h.getShort(0).toInt() and 0xffff
        if (magic != 0xF30A) { if (strict) throw IOException("EXT4: invalid extent header") else return }
        val entries = h.getShort(2).toInt() and 0xffff
        val depth = h.getShort(6).toInt() and 0xffff
        if (strict && (depth > 5 || entries > (h.limit() - 12) / 12 || out.size + entries > maxEntries)) throw IOException("EXT4: invalid extent entries")
        for (i in 0 until entries) {
            val e = 12 + i * 12
            if (depth == 0) {
                val lblk = h.getInt(e).toLong() and 0xffffffffL
                var len = h.getShort(e + 4).toInt() and 0xffff
                val uninit = len > 32768
                if (uninit) len -= 32768
                val hi = h.getShort(e + 6).toLong() and 0xffff
                val lo = h.getInt(e + 8).toLong() and 0xffffffffL
                val physical = (hi shl 32) or lo
                if (strict && (len == 0 || physical > src.size / blockSize || len.toLong() > src.size / blockSize - physical)) throw IOException("EXT4: extent outside image")
                if (!uninit) out.add(longArrayOf(lblk, physical, len.toLong()))
            } else {
                val lo = h.getInt(e + 4).toLong() and 0xffffffffL
                val hi = h.getShort(e + 8).toLong() and 0xffff
                val child = (hi shl 32) or lo
                if (strict && !visited.add(child)) throw IOException("EXT4: cyclic extent tree")
                val node = readBlock(child)
                if (strict && ((node.getShort(6).toInt() and 0xffff) != depth - 1)) throw IOException("EXT4: inconsistent extent depth")
                walkExtents(node, out, level + 1, visited)
            }
        }
    }

    /** Пишет содержимое файла в поток. */
    fun copy(n: Node, out: OutputStream) {
        checkCancelled()
        if (strict && (n.size < 0 || n.size > maxFileBytes)) throw IOException("EXT4: oversized file")
        if (n.flags and 0x10000000 != 0) { // INLINE_DATA
            if (strict && n.size > 60) throw IOException("EXT4: unsupported inline data")
            out.write(n.block, 0, minOf(n.size, 60).toInt())
            return
        }
        val ex = extents(n).sortedBy { it[0] }
        var pos = 0L
        val chunk = ByteArray(1 shl 20)
        val zeros = ByteArray(64 * 1024)
        fun zero(len: Long) {
            var left = len
            while (left > 0) { checkCancelled(); val k = minOf(left, zeros.size.toLong()).toInt(); out.write(zeros, 0, k); left -= k }
        }
        for (e in ex) {
            val start = e[0] * blockSize
            if (strict && start < pos) throw IOException("EXT4: overlapping extents")
            if (start >= n.size) break
            if (start > pos) { zero(start - pos); pos = start }
            var left = minOf(e[2] * blockSize, n.size - start)
            var phys = e[1] * blockSize
            while (left > 0) {
                val k = minOf(left, chunk.size.toLong()).toInt()
                val bb = ByteBuffer.wrap(chunk, 0, k)
                readAt(phys, bb)
                out.write(chunk, 0, k)
                phys += k; left -= k; pos += k
            }
        }
        if (pos < n.size) zero(n.size - pos)
    }

    fun readAll(n: Node): ByteArray {
        if (strict && (n.size < 0 || n.size > 16L * 1024 * 1024)) throw IOException("EXT4: oversized in-memory file")
        val bo = java.io.ByteArrayOutputStream(n.size.toInt().coerceAtLeast(0))
        copy(n, bo)
        return bo.toByteArray()
    }

    fun linkTarget(n: Node): String {
        if (strict && (n.size < 0 || n.size > 4096)) throw IOException("EXT4: invalid link size")
        // быстрые ссылки (<60 байт) живут прямо в i_block
        return if (n.size < 60 && n.flags and 0x80000 == 0 && n.flags and 0x10000000 == 0) String(n.block, 0, n.size.toInt())
        else String(readAll(n))
    }

    data class Entry(val name: String, val ino: Int, val type: Int)

    fun list(dir: Node): List<Entry> {
        if (strict && (!dir.isDir || dir.size > 16L * 1024 * 1024)) throw IOException("EXT4: invalid directory")
        val data = readAll(dir)
        val out = ArrayList<Entry>()
        val bb = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
        var off = 0
        val filetype = incompat and 0x2 != 0
        while (off + 8 <= data.size) {
            val ino = bb.getInt(off)
            val recLen = bb.getShort(off + 4).toInt() and 0xffff
            val nameLen = if (filetype) bb.get(off + 6).toInt() and 0xff else bb.getShort(off + 6).toInt() and 0xffff
            val type = if (filetype) bb.get(off + 7).toInt() and 0xff else 0
            if (recLen < 8 || (strict && (recLen % 4 != 0 || recLen > data.size - off || nameLen > recLen - 8))) {
                if (strict) throw IOException("EXT4: invalid directory entry") else break
            }
            if (ino != 0 && nameLen > 0 && off + 8 + nameLen <= data.size) {
                val name = String(data, off + 8, nameLen, Charsets.UTF_8)
                if (strict && (ino < 1 || ino > inodeCount || name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains('\u0000'))) throw IOException("EXT4: invalid directory name")
                if (name != "." && name != ".." && type != 0xDE) out.add(Entry(name, ino, type))
            }
            off += recLen
        }
        if (strict && off != data.size) throw IOException("EXT4: truncated directory")
        return out
    }

    /** Обход дерева: visitor получает путь относительно корня образа и inode. */
    fun walk(visitor: (path: String, node: Node) -> Unit) {
        val stack = ArrayDeque<Pair<String, Int>>()
        stack.add("" to 2)
        val seen = HashSet<Int>()
        var visitedEntries = 0
        while (stack.isNotEmpty()) {
            checkCancelled()
            val (path, ino) = stack.removeLast()
            if (strict && (++visitedEntries > maxEntries || path.length > 8192 || path.count { it == '/' } > 128)) throw IOException("EXT4: tree limit")
            val n = inode(ino)
            if (strict && !n.isDir && !n.isFile && !n.isLink) throw IOException("EXT4: unsupported inode type")
            if (path.isNotEmpty()) visitor(path, n)
            if (strict && n.isDir && !seen.add(ino)) throw IOException("EXT4: repeated directory inode")
            if (n.isDir && (strict || seen.add(ino))) {
                for (e in list(n)) {
                    if (path.isEmpty() && e.name == "lost+found") continue
                    stack.add((if (path.isEmpty()) e.name else "$path/${e.name}") to e.ino)
                }
            }
        }
    }

    fun lookup(path: String): Node? {
        var n = inode(2)
        for (part in path.trim('/').split('/').filter { it.isNotEmpty() }) {
            if (!n.isDir) return null
            val e = list(n).firstOrNull { it.name == part } ?: return null
            n = inode(e.ino)
        }
        return n
    }

    companion object {
        fun probe(src: RandomSource): Boolean = runCatching {
            val b = ByteBuffer.allocate(2).order(ByteOrder.LITTLE_ENDIAN)
            src.read(1024 + 0x38, b); b.flip()
            b.getShort(0).toInt() and 0xffff == 0xEF53
        }.getOrDefault(false)
    }
}
