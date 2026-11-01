package app.aemu.core

import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.security.MessageDigest
import java.security.PrivateKey
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.Mac
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * The server side of TLS 1.0 (RFC 2246) for the guest's own TLS, which the phone's TLS (1.2/1.3 only) no longer
 * speaks: 2.2's ClientHello is 16 03 01 (TLS 1.0, AES128-SHA, no SNI). RSA key exchange and the CBC suites of that
 * time, built from the platform's primitives: TLS_RSA_WITH_AES_128_CBC_SHA (002f), _AES_256_CBC_SHA (0035),
 * _3DES_EDE_CBC_SHA (000a). [input]/[output] carry the application data once [handshake] is done.
 */
class Tls10Server(sock: java.net.Socket, private val key: PrivateKey, private val chain: List<ByteArray>) {
    private val inp = DataInputStream(sock.getInputStream())
    private val out = sock.getOutputStream()
    private val rnd = SecureRandom()
    private val hs = ByteArrayOutputStream()            // every handshake message, for Finished
    private var ver = 0x0301
    private var suite = 0
    private var readC: Cipher? = null; private var readMac: Mac? = null; private var readSeq = 0L
    private var writeC: Cipher? = null; private var writeMac: Mac? = null; private var writeSeq = 0L
    private var writeIv = ByteArray(0)
    private var pending = ByteArray(0)                   // handshake bytes read but not parsed

    private class Rec(val type: Int, val data: ByteArray)

    private fun readRecord(): Rec {
        val type = inp.readUnsignedByte(); inp.readUnsignedShort()
        val n = inp.readUnsignedShort()
        if (n > 18432) throw IOException("record of $n bytes")
        val b = ByteArray(n); inp.readFully(b)
        val c = readC ?: return Rec(type, b)
        val p = c.update(b)
        val pad = (p.last().toInt() and 0xff) + 1
        val macLen = readMac!!.macLength
        if (p.size < pad + macLen) throw IOException("bad padding")
        val data = p.copyOfRange(0, p.size - pad - macLen)
        val want = mac(readMac!!, readSeq++, type, data)
        if (!MessageDigest.isEqual(want, p.copyOfRange(data.size, data.size + macLen))) throw IOException("bad record MAC")
        return Rec(type, data)
    }

    private fun mac(m: Mac, seq: Long, type: Int, data: ByteArray): ByteArray {
        val h = ByteArray(13)
        for (i in 0 until 8) h[i] = (seq ushr (56 - 8 * i)).toByte()
        h[8] = type.toByte(); h[9] = (ver shr 8).toByte(); h[10] = ver.toByte(); h[11] = (data.size shr 8).toByte(); h[12] = data.size.toByte()
        m.update(h); return m.doFinal(data)
    }

    private fun writeRecord(type: Int, data: ByteArray) {
        var off = 0
        do {
            val n = minOf(16384, data.size - off)
            var body = data.copyOfRange(off, off + n)
            val c = writeC
            if (c != null) {
                val m = mac(writeMac!!, writeSeq++, type, body)
                val bs = c.blockSize
                val padLen = bs - (body.size + m.size + 1) % bs
                val plain = body + m + ByteArray(padLen + 1) { padLen.toByte() }
                body = c.update(plain)
            }
            out.write(byteArrayOf(type.toByte(), (ver shr 8).toByte(), ver.toByte(), (body.size shr 8).toByte(), body.size.toByte()))
            out.write(body)
            off += n
        } while (off < data.size)
        out.flush()
    }

    /** the next whole handshake message (type, body); records may split or join them */
    private fun nextHandshake(): Pair<Int, ByteArray> {
        while (pending.size < 4 || pending.size < 4 + ((pending[1].toInt() and 0xff) shl 16 or ((pending[2].toInt() and 0xff) shl 8) or (pending[3].toInt() and 0xff))) {
            val r = readRecord()
            if (r.type == 21) throw IOException("alert ${r.data.joinToString { (it.toInt() and 0xff).toString() }}")
            if (r.type != 22) throw IOException("record ${r.type} in the handshake")
            pending += r.data
        }
        val n = (pending[1].toInt() and 0xff) shl 16 or ((pending[2].toInt() and 0xff) shl 8) or (pending[3].toInt() and 0xff)
        val msg = pending.copyOfRange(0, 4 + n)
        pending = pending.copyOfRange(4 + n, pending.size)
        return (msg[0].toInt() and 0xff) to msg
    }

    private fun hsMsg(type: Int, body: ByteArray) = byteArrayOf(type.toByte(), (body.size shr 16).toByte(), (body.size shr 8).toByte(), body.size.toByte()) + body
    private fun u24(n: Int) = byteArrayOf((n shr 16).toByte(), (n shr 8).toByte(), n.toByte())

    private fun hmacHash(alg: String, secret: ByteArray, seed: ByteArray, n: Int): ByteArray {
        val m = Mac.getInstance(alg); m.init(SecretKeySpec(if (secret.isEmpty()) ByteArray(1) else secret, alg))
        val o = ByteArrayOutputStream(); var a = seed
        while (o.size() < n) { a = m.doFinal(a); m.update(a); o.write(m.doFinal(seed)) }
        return o.toByteArray().copyOf(n)
    }
    /** RFC 2246 5: P_MD5 over the first half of the secret xor P_SHA-1 over the second */
    private fun prf(secret: ByteArray, label: String, seed: ByteArray, n: Int): ByteArray {
        val half = (secret.size + 1) / 2
        val s1 = secret.copyOfRange(0, half); val s2 = secret.copyOfRange(secret.size - half, secret.size)
        val ls = label.toByteArray() + seed
        val a = hmacHash("HmacMD5", s1, ls, n); val b = hmacHash("HmacSHA1", s2, ls, n)
        return ByteArray(n) { (a[it].toInt() xor b[it].toInt()).toByte() }
    }
    private fun hsHash(): ByteArray {
        val d = hs.toByteArray()
        return MessageDigest.getInstance("MD5").digest(d) + MessageDigest.getInstance("SHA-1").digest(d)
    }

    fun handshake(): String {
        val (t, ch) = nextHandshake()
        if (t != 1) throw IOException("expected ClientHello, got $t")
        hs.write(ch)
        val b = ch.copyOfRange(4, ch.size)
        val cv = (b[0].toInt() and 0xff) shl 8 or (b[1].toInt() and 0xff)
        ver = minOf(cv, 0x0301).coerceAtLeast(0x0301)
        val cr = b.copyOfRange(2, 34)
        var p = 34
        p += 1 + (b[p].toInt() and 0xff)
        val nSuites = ((b[p].toInt() and 0xff) shl 8 or (b[p + 1].toInt() and 0xff)) / 2; p += 2
        val offered = (0 until nSuites).map { (b[p + 2 * it].toInt() and 0xff) shl 8 or (b[p + 2 * it + 1].toInt() and 0xff) }
        suite = listOf(0x002f, 0x0035, 0x000a).firstOrNull { it in offered } ?: throw IOException("no common suite in ${offered.joinToString { "%04x".format(it) }}")
        val sr = ByteArray(32).also { rnd.nextBytes(it) }
        val sid = ByteArray(32).also { rnd.nextBytes(it) }
        val hello = hsMsg(2, byteArrayOf((ver shr 8).toByte(), ver.toByte()) + sr + byteArrayOf(32) + sid + byteArrayOf((suite shr 8).toByte(), suite.toByte(), 0))
        val certs = ByteArrayOutputStream().apply { chain.forEach { write(u24(it.size)); write(it) } }.toByteArray()
        val cert = hsMsg(11, u24(certs.size) + certs)
        val done = hsMsg(14, ByteArray(0))
        hs.write(hello); hs.write(cert); hs.write(done)
        writeRecord(22, hello + cert + done)

        val (t2, cke) = nextHandshake()
        if (t2 != 16) throw IOException("expected ClientKeyExchange, got $t2")
        hs.write(cke)
        val enc = cke.copyOfRange(6, cke.size)   // TLS 1.0: a 2-byte length in front of the RSA block
        val rsa = Cipher.getInstance("RSA/ECB/PKCS1Padding").apply { init(Cipher.DECRYPT_MODE, key) }
        // RFC 2246 7.4.7.1 / Bleichenbacher: a bad block goes on with random bytes and fails at Finished
        val pre = runCatching { rsa.doFinal(enc) }.getOrNull()?.takeIf { it.size == 48 } ?: ByteArray(48).also { rnd.nextBytes(it) }
        val master = prf(pre, "master secret", cr + sr, 48)
        val (kl, cname, bl) = when (suite) { 0x002f -> Triple(16, "AES", 16); 0x0035 -> Triple(32, "AES", 16); else -> Triple(24, "DESede", 8) }
        val kb = prf(master, "key expansion", sr + cr, 2 * 20 + 2 * kl + 2 * bl)
        var o = 0
        fun take(n: Int) = kb.copyOfRange(o, o + n).also { o += n }
        val cMac = take(20); val sMac = take(20); val cKey = take(kl); val sKey = take(kl); val cIv = take(bl); val sIv = take(bl)
        val alg = "$cname/CBC/NoPadding"

        val ccs = readRecord()
        if (ccs.type != 20) throw IOException("expected ChangeCipherSpec, got record ${ccs.type}")
        readC = Cipher.getInstance(alg).apply { init(Cipher.DECRYPT_MODE, SecretKeySpec(cKey, cname), IvParameterSpec(cIv)) }
        readMac = Mac.getInstance("HmacSHA1").apply { init(SecretKeySpec(cMac, "HmacSHA1")) }
        val expect = prf(master, "client finished", hsHash(), 12)
        val (t3, fin) = nextHandshake()
        if (t3 != 20 || !MessageDigest.isEqual(expect, fin.copyOfRange(4, fin.size))) throw IOException("client Finished does not match")
        hs.write(fin)
        writeRecord(20, byteArrayOf(1))
        writeC = Cipher.getInstance(alg).apply { init(Cipher.ENCRYPT_MODE, SecretKeySpec(sKey, cname), IvParameterSpec(sIv)) }
        writeMac = Mac.getInstance("HmacSHA1").apply { init(SecretKeySpec(sMac, "HmacSHA1")) }
        writeRecord(22, hsMsg(20, prf(master, "server finished", hsHash(), 12)))
        return "%04x".format(suite)
    }

    val input: InputStream = object : InputStream() {
        private var buf = ByteArray(0); private var pos = 0
        private fun fill(): Boolean {
            while (pos >= buf.size) {
                val r = runCatching { readRecord() }.getOrNull() ?: return false
                when (r.type) { 23 -> { buf = r.data; pos = 0 }; 21 -> return false; else -> {} }
            }
            return true
        }
        override fun read(): Int = if (fill()) buf[pos++].toInt() and 0xff else -1
        override fun read(b: ByteArray, off: Int, len: Int): Int {
            if (!fill()) return -1
            val n = minOf(len, buf.size - pos); System.arraycopy(buf, pos, b, off, n); pos += n; return n
        }
    }
    val output: OutputStream = object : OutputStream() {
        override fun write(b: Int) = write(byteArrayOf(b.toByte()), 0, 1)
        override fun write(b: ByteArray, off: Int, len: Int) { if (len > 0) writeRecord(23, b.copyOfRange(off, off + len)) }
        override fun close() { runCatching { writeRecord(21, byteArrayOf(1, 0)) } }
    }
}
