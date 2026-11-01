package app.aemu.core

import android.content.Context
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.math.BigInteger
import java.net.InetAddress
import java.net.ServerSocket
import java.net.Socket
import java.security.KeyFactory
import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.Signature
import java.security.cert.CertificateFactory
import java.security.cert.X509Certificate
import java.security.spec.PKCS8EncodedKeySpec
import java.util.concurrent.ConcurrentHashMap
import javax.net.ssl.KeyManagerFactory
import javax.net.ssl.SSLContext
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory

/**
 * TLS for firmwares whose own TLS no longer reaches today's servers (1.5-2.2: their handshake with www.google.com
 * times out). Kernel 23 (run64.c, AEMU64_TLS) sends a guest's TCP connect to port 443 here and first tells which
 * address it was for ("M <local port> <a.b.c.d>"), and copies every DNS query ("Q" + packet): those clients send no
 * SNI, so the name comes from what the guest looked up. The guest's TLS ends here with a certificate for that name
 * signed by the emulator's own root (put into the guest's trust store by [installCa]); the onward connection is the
 * phone's own TLS with SNI and the real server's certificate checked. Nothing of the phone's own trust is touched.
 */
class TlsBridge(private val ctx: Context, private val paths: VmPaths, private val log: (String) -> Unit) {
    private var server: ServerSocket? = null
    private var mapFd = -1
    private val mapped = ConcurrentHashMap<Int, String>()          // guest local port -> destination address
    private val names get() = seen                                   // address -> name the guest resolved to it
    @Volatile private var lastName: Pair<String, Long>? = null
    private val contexts = ConcurrentHashMap<String, SSLContext>()
    private val leaves = ConcurrentHashMap<String, ByteArray>()
    @Volatile private var logged = 0
    private val cur = ThreadLocal<String>()
    @Volatile private var qn = 0
    val mapPath: File get() = File(paths.bin, "tls.sock")

    fun start(): Int {
        val ca = runCatching { Ca.load(ctx) }.getOrElse { log("tls: no root: ${it.message}"); return 0 }
        val s = runCatching { ServerSocket(0, 64, InetAddress.getByName("127.0.0.1")) }.getOrElse { log("tls: ${it.message}"); return 0 }
        server = s
        runCatching { installCa(ca) }.onFailure { log("tls: trust store: ${it.message}") }
        mapPath.delete()
        mapFd = HostNative.dgramBind(mapPath.absolutePath)
        if (mapFd < 0) log("tls: map socket: errno ${-mapFd}")
        else Thread({ notes() }, "tls-map").apply { isDaemon = true }.start()
        Thread({ accept(s, ca) }, "tls-accept").apply { isDaemon = true }.start()
        log("tls: bridge on 127.0.0.1:${s.localPort}")
        return s.localPort
    }

    /**
     * The root goes into the guest's /system/etc/security/cacerts.bks (BouncyCastle "BKS" v1/v2, JDKKeyStore: header
     * version/salt/iterations, entries [type, alias UTF, date long, chain int, certificate: type UTF, length int,
     * DER], a 0 byte, then the HMAC). Android loads it with no password, and its BouncyCastle then skips the HMAC, so
     * the entry is added in front of the 0 byte and the rest is kept. The firmware's file stays as cacerts.bks.rom.
     */
    private fun installCa(ca: Ca) {
        // 4.0+: a directory of PEM files named <subject hash>.0 (libcore TrustedCertificateStore), the hash being
        // OpenSSL's X509_NAME_hash_old: the first 4 bytes of MD5 over the subject's DER, little-endian
        val dir = File(paths.root, "system/etc/security/cacerts")
        if (dir.isDirectory) {
            val md5 = java.security.MessageDigest.getInstance("MD5").digest(ca.cert.subjectX500Principal.encoded)
            val h = (md5[0].toLong() and 0xff) or ((md5[1].toLong() and 0xff) shl 8) or ((md5[2].toLong() and 0xff) shl 16) or ((md5[3].toLong() and 0xff) shl 24)
            val pem = "-----BEGIN CERTIFICATE-----\n" + android.util.Base64.encodeToString(ca.cert.encoded, android.util.Base64.DEFAULT) + "-----END CERTIFICATE-----\n"
            val f = File(dir, "%08x.0".format(h))
            if (!f.isFile || f.readText() != pem) { f.writeText(pem); log("tls: root added to the guest's trust store (${f.name})") }
        }
        val f = File(paths.root, "system/etc/security/cacerts.bks")
        val rom = File(paths.root, "system/etc/security/cacerts.bks.rom")
        if (!rom.isFile) { if (!f.isFile) return; f.copyTo(rom) }
        val b = rom.readBytes()
        val inp = java.io.DataInputStream(b.inputStream())
        inp.readInt(); inp.skipBytes(inp.readInt()); inp.readInt()
        fun skipUtf() { inp.skipBytes(inp.readUnsignedShort()) }
        fun skipCert() { skipUtf(); inp.skipBytes(inp.readInt()) }
        while (true) {
            val t = inp.read()
            if (t <= 0) break
            skipUtf(); inp.readLong()
            repeat(inp.readInt()) { skipCert() }
            when (t) {
                1 -> skipCert()
                2 -> { inp.read(); skipUtf(); skipUtf(); inp.skipBytes(inp.readInt()) }
                else -> inp.skipBytes(inp.readInt())
            }
        }
        val end = b.size - inp.available() - 1        // the 0 byte
        val e = ByteArrayOutputStream()
        java.io.DataOutputStream(e).apply {
            write(1); writeUTF("aemu-tls-bridge"); writeLong(System.currentTimeMillis()); writeInt(0)
            writeUTF("X.509"); writeInt(ca.cert.encoded.size); write(ca.cert.encoded); flush()
        }
        val out = b.copyOfRange(0, end) + e.toByteArray() + b.copyOfRange(end, b.size)
        if (!f.isFile || !f.readBytes().contentEquals(out)) { f.writeBytes(out); log("tls: root added to the guest's trust store") }
    }

    fun stop() {
        runCatching { server?.close() }; server = null
        if (mapFd >= 0) HostNative.dgramClose(mapFd); mapFd = -1
    }

    private fun notes() {
        val buf = ByteArray(2048); val from = arrayOfNulls<String>(1)
        while (true) {
            val n = HostNative.dgramRecv(mapFd, buf, from)
            if (n <= 0) break
            if (buf[0] == 'M'.code.toByte()) {
                val t = String(buf, 0, n).trim().split(' ')
                if (t.size == 3) t[1].toIntOrNull()?.let { mapped[it] = t[2] }
            } else if (buf[0] == 'Q'.code.toByte()) {
                val name = qname(buf, 1, n) ?: continue
                lastName = name to System.currentTimeMillis()
                if (qn++ < 3) log("tls: dns $name")
                Thread({ runCatching { InetAddress.getAllByName(name).forEach { names[it.hostAddress ?: ""] = name } } }, "tls-dns")
                    .apply { isDaemon = true }.start()
            }
        }
    }

    /** server_name (RFC 6066 3) of a ClientHello in [b]: 2.3+ clients send it, and it beats any guess */
    private fun sni(b: ByteArray, n: Int): String? = runCatching {
        if (n < 43 || b[0] != 0x16.toByte() || b[5] != 1.toByte()) return null
        var p = 9 + 2 + 32
        p += 1 + (b[p].toInt() and 0xff)
        p += 2 + ((b[p].toInt() and 0xff) shl 8 or (b[p + 1].toInt() and 0xff))
        p += 1 + (b[p].toInt() and 0xff)
        val end = minOf(n, p + 2 + ((b[p].toInt() and 0xff) shl 8 or (b[p + 1].toInt() and 0xff))); p += 2
        while (p + 4 <= end) {
            val t = (b[p].toInt() and 0xff) shl 8 or (b[p + 1].toInt() and 0xff)
            val l = (b[p + 2].toInt() and 0xff) shl 8 or (b[p + 3].toInt() and 0xff)
            if (t == 0 && p + 9 <= end) {
                val nl = (b[p + 7].toInt() and 0xff) shl 8 or (b[p + 8].toInt() and 0xff)
                return String(b, p + 9, nl, Charsets.US_ASCII)
            }
            p += 4 + l
        }
        null
    }.getOrNull()

    /** the first question's name of a DNS query (RFC 1035 4.1.2) */
    private fun qname(b: ByteArray, off: Int, end: Int): String? {
        var i = off + 12
        val sb = StringBuilder()
        while (i < end) {
            val l = b[i].toInt() and 0xff
            if (l == 0) return sb.toString().ifEmpty { null }
            if (l >= 64 || i + 1 + l > end) return null
            if (sb.isNotEmpty()) sb.append('.')
            sb.append(String(b, i + 1, l, Charsets.US_ASCII))
            i += 1 + l
        }
        return null
    }

    private fun accept(s: ServerSocket, ca: Ca) {
        while (true) {
            val c = runCatching { s.accept() }.getOrNull() ?: break
            Thread({ runCatching { serve(c, ca) }.onFailure { log("tls: ${cur.get() ?: "?"}: ${it.javaClass.simpleName}: ${it.message}") }; runCatching { c.close() } }, "tls-conn")
                .apply { isDaemon = true }.start()
        }
    }

    private fun serve(c: Socket, ca: Ca) {
        var ip: String? = null
        for (i in 0 until 50) { ip = mapped.remove(c.port); if (ip != null) break; Thread.sleep(10) }
        if (ip == null) { log("tls: no destination for port ${c.port}"); return }
        val head = ByteArray(2048)
        val got = runCatching {
            val pfd = android.os.ParcelFileDescriptor.fromSocket(c)
            Thread.sleep(50)
            val n = android.system.Os.recvfrom(pfd.fileDescriptor, head, 0, head.size, android.system.OsConstants.MSG_PEEK, null)
            pfd.close(); n
        }.getOrDefault(0)
        val recent = lastName?.takeIf { System.currentTimeMillis() - it.second < 30_000 }?.first
        val name = sni(head, got) ?: names[ip] ?: recent ?: ip
        cur.set("$name ($ip)")
        // ClientHello's client_version (record 16 03 xx, handshake 01, length, then the version): up to TLS 1.0 the
        // phone's TLS cannot answer, our own TLS 1.0 server does
        val old = got >= 11 && head[0] == 0x16.toByte() && head[5] == 1.toByte() && ((head[9].toInt() and 0xff) shl 8 or (head[10].toInt() and 0xff)) <= 0x0301
        val gin: InputStream; val gout: OutputStream
        var up0: SSLSocket? = null
        if (old) {
            // the real server first (its chain checked by the phone's trust store, SNI = the guest's name or a guess);
            // the guest gets a certificate with the real one's names, and checks its host name against them itself
            up0 = (SSLSocketFactory.getDefault().createSocket(ip, 443) as SSLSocket).apply {
                if (!name[0].isDigit()) sslParameters = javax.net.ssl.SSLParameters().apply { serverNames = listOf(javax.net.ssl.SNIHostName(name)) }
                startHandshake()
            }
            val real = up0.session.peerCertificates[0] as X509Certificate
            val sans = real.subjectAlternativeNames?.filter { it[0] == 2 }?.map { it[1] as String }.orEmpty()
            val cn = real.subjectX500Principal.name.split(',').firstOrNull { it.startsWith("CN=") }?.substring(3)
            val all = (listOfNotNull(cn) + sans).distinct().ifEmpty { listOf(name) }
            val t = Tls10Server(c, ca.leafKeys.private, listOf(leaves.getOrPut(all.joinToString(",")) { ca.leafDer(all) }, ca.cert.encoded))
            val suite = t.handshake()
            if (logged++ < 3) log("tls: $name over TLS 1.0 ($suite)")
            gin = t.input; gout = t.output
        } else {
            // a client of TLS 1.1 or later reaches servers itself: its bytes go through untouched (no certificate
            // of ours, its own pinning and checks stay as they are)
            val raw = Socket(ip, 443)
            val t = Thread({ pipe(raw.getInputStream(), c.getOutputStream()); runCatching { c.close() } }, "tls-raw").apply { isDaemon = true }
            t.start()
            pipe(c.getInputStream(), raw.getOutputStream())
            runCatching { raw.close() }
            t.join(2000)
            return
        }
        val up = up0 ?: return
        val t = Thread({ pipe(up.inputStream, gout); runCatching { gout.close(); c.close() } }, "tls-down").apply { isDaemon = true }
        t.start()
        pipe(gin, up.outputStream)
        runCatching { up.close() }
        t.join(2000)
    }

    private fun pipe(i: InputStream, o: OutputStream) {
        val b = ByteArray(16384)
        runCatching { while (true) { val n = i.read(b); if (n < 0) break; o.write(b, 0, n); o.flush() } }
    }

    companion object {
        private val seen = ConcurrentHashMap<String, String>()
        /** what the guest resolved through the host (DnsProxy: netd's dnsproxyd on 4.3+), for clients without SNI */
        fun resolved(name: String, addrs: List<InetAddress>) { for (a in addrs) a.hostAddress?.let { seen[it] = name } }
    }

    /** The emulator's root: made once per install (files/tls/), the guest's trust store gets its certificate. */
    class Ca(val key: PrivateKey, val cert: X509Certificate, val leafKeys: KeyPair) {
        fun leafDer(names: List<String>): ByteArray = Der.cert(names, leafKeys.public.encoded, key, cert.subjectX500Principal.encoded, ca = false)
        fun serverContext(name: String): SSLContext {
            val leaf = leafDer(listOf(name))
            val cf = CertificateFactory.getInstance("X.509")
            val lc = cf.generateCertificate(leaf.inputStream()) as X509Certificate
            val ks = KeyStore.getInstance(KeyStore.getDefaultType()).apply { load(null) }
            ks.setKeyEntry("k", leafKeys.private, CharArray(0), arrayOf(lc, cert))
            val kmf = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()).apply { init(ks, CharArray(0)) }
            return SSLContext.getInstance("TLS").apply { init(kmf.keyManagers, null, null) }
        }

        companion object {
            fun dir(ctx: Context) = File(ctx.filesDir, "tls").apply { mkdirs() }
            @Synchronized
            fun load(ctx: Context): Ca {
                val d = dir(ctx)
                val kf = KeyFactory.getInstance("RSA")
                val cf = CertificateFactory.getInstance("X.509")
                fun pair(n: String): KeyPair {
                    val kFile = File(d, "$n.key"); val pFile = File(d, "$n.pub")
                    if (kFile.isFile && pFile.isFile) return KeyPair(
                        kf.generatePublic(java.security.spec.X509EncodedKeySpec(pFile.readBytes())),
                        kf.generatePrivate(PKCS8EncodedKeySpec(kFile.readBytes())))
                    val kp = KeyPairGenerator.getInstance("RSA").apply { initialize(2048) }.generateKeyPair()
                    kFile.writeBytes(kp.private.encoded); pFile.writeBytes(kp.public.encoded)
                    return kp
                }
                val root = pair("root")
                val cFile = File(d, "root.crt")
                if (!cFile.isFile) cFile.writeBytes(Der.cert(listOf("AEmulator TLS bridge"), root.public.encoded, root.private, null, ca = true))
                val cert = cf.generateCertificate(cFile.inputStream()) as X509Certificate
                return Ca(root.private, cert, pair("leaf"))
            }
        }
    }

    /** Just enough DER (X.690) for an X.509 v3 certificate (RFC 5280 4.1), SHA1withRSA as 1.x-2.x verify it. */
    object Der {
        private fun len(n: Int): ByteArray = when {
            n < 0x80 -> byteArrayOf(n.toByte())
            n < 0x100 -> byteArrayOf(0x81.toByte(), n.toByte())
            n < 0x10000 -> byteArrayOf(0x82.toByte(), (n shr 8).toByte(), n.toByte())
            else -> byteArrayOf(0x83.toByte(), (n shr 16).toByte(), (n shr 8).toByte(), n.toByte())
        }
        fun tlv(tag: Int, vararg parts: ByteArray): ByteArray {
            val body = ByteArrayOutputStream().apply { parts.forEach { write(it) } }.toByteArray()
            return byteArrayOf(tag.toByte()) + len(body.size) + body
        }
        private fun seq(vararg p: ByteArray) = tlv(0x30, *p)
        private fun oid(vararg arcs: Long): ByteArray {
            val o = ByteArrayOutputStream(); o.write((arcs[0] * 40 + arcs[1]).toInt())
            for (a in arcs.drop(2)) {
                val tmp = ArrayList<Int>(); var v = a; tmp.add((v and 0x7f).toInt()); v = v shr 7
                while (v > 0) { tmp.add(((v and 0x7f) or 0x80).toInt()); v = v shr 7 }
                tmp.reversed().forEach { o.write(it) }
            }
            return tlv(0x06, o.toByteArray())
        }
        private fun int(v: BigInteger) = tlv(0x02, v.toByteArray())
        private fun utc(ms: Long) = tlv(0x17, java.text.SimpleDateFormat("yyMMddHHmmss'Z'", java.util.Locale.US)
            .apply { timeZone = java.util.TimeZone.getTimeZone("UTC") }.format(java.util.Date(ms)).toByteArray())
        private fun name(cn: String) = seq(tlv(0x31, seq(oid(2, 5, 4, 3), tlv(0x0c, cn.toByteArray()))))
        private val sha1Rsa = seq(oid(1, 2, 840, 113549, 1, 1, 5), byteArrayOf(0x05, 0x00))

        fun cert(names: List<String>, pub: ByteArray, signKey: PrivateKey, issuer: ByteArray?, ca: Boolean): ByteArray {
            val cn = names[0]
            val now = System.currentTimeMillis()
            val day = 86_400_000L
            // UTCTime ends in 2049: the root lives until then, a site's certificate a year (re-made every start)
            val exts = ArrayList<ByteArray>()
            if (ca) exts += seq(oid(2, 5, 29, 19), byteArrayOf(0x01, 0x01, 0xff.toByte()), tlv(0x04, seq(byteArrayOf(0x01, 0x01, 0xff.toByte()))))
            else {
                val gns = names.map { n -> if (n.matches(Regex("[0-9.]+"))) tlv(0x87, n.split('.').map { it.toInt().toByte() }.toByteArray()) else tlv(0x82, n.toByteArray()) }
                exts += seq(oid(2, 5, 29, 17), tlv(0x04, seq(*gns.toTypedArray())))
            }
            val tbs = seq(
                tlv(0xa0, int(BigInteger.valueOf(2))),
                int(BigInteger.valueOf(now).add(BigInteger.valueOf(cn.hashCode().toLong() and 0xffffffL).shiftLeft(48))),
                sha1Rsa,
                issuer ?: name(cn),
                seq(utc(now - 2 * day), utc(if (ca) 2_500_000_000_000L else now + 365 * day)),
                name(cn),
                pub,
                tlv(0xa3, seq(*exts.toTypedArray())),
            )
            val sig = Signature.getInstance("SHA1withRSA").apply { initSign(signKey); update(tbs) }.sign()
            return seq(tbs, sha1Rsa, tlv(0x03, byteArrayOf(0) + sig))
        }
    }
}
