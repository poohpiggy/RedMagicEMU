package app.aemu.core
import java.net.InetAddress
import java.net.Inet4Address
import java.net.Inet6Address
import java.io.ByteArrayOutputStream
internal object AddressDnsReply {
    private fun u16(data: ByteArray, i: Int) = ((data[i].toInt() and 255) shl 8) or (data[i+1].toInt() and 255)
    fun query(packet: ByteArray, flags: Int, lookup: (String) -> List<InetAddress>): DnsRawProtocol.Result {
        val invalid=DnsRawProtocol.Result.Error(22 /* EINVAL */)
        val unsupported=DnsRawProtocol.Result.Error(95 /* ENOTSUP */)
        if (flags != 0) return unsupported
        if (packet.size < 17) return invalid
        val queryFlags=u16(packet,2)
        if ((queryFlags and 0xfeff) != 0 || u16(packet,4)!=1 || u16(packet,6)!=0 ||
            u16(packet,8)!=0 || u16(packet,10)!=0) return unsupported
        var cursor=12
        val labels=ArrayList<String>()
        while (true) {
            if (cursor >= packet.size) return invalid
            val len=packet[cursor++].toInt() and 255
            if (len==0) break
            if (len>63 || cursor+len>packet.size) return invalid
            val bytes=packet.copyOfRange(cursor,cursor+len)
            if (bytes.any { val c=it.toInt() and 255; c !in 33..126 || c==46 || c==92 }) return invalid
            labels.add(String(bytes,Charsets.US_ASCII))
            cursor+=len
        }
        if (labels.isEmpty() || cursor+4 != packet.size) return unsupported
        val name=labels.joinToString(".",postfix=".")
        if (name.length>254) return invalid
        val type=u16(packet,cursor); val klass=u16(packet,cursor+2)
        if ((type!=1 && type!=28) || klass!=1) return unsupported
        val resolved=try { lookup(name) } catch (_: Exception) { return DnsRawProtocol.Result.Error(5 /* EIO */) }
        val selected=resolved.filter { if (type==1) it is Inet4Address else it is Inet6Address }.distinctBy { it.hostAddress }
        // getAllByName does not reveal authoritative NODATA for the requested family.
        if (selected.isEmpty()) return unsupported
        if (selected.size>128) return DnsRawProtocol.Result.Error(90 /* EMSGSIZE */)
        val output=ByteArrayOutputStream(packet.size+selected.size*28)
        fun u16out(v: Int) { output.write((v ushr 8) and 255); output.write(v and 255) }
        output.write(packet,0,2); u16out(0x8080 or (queryFlags and 0x0100))
        u16out(1); u16out(selected.size); u16out(0); u16out(0)
        output.write(packet,12,packet.size-12)
        for (address in selected) {
            u16out(0xc00c); u16out(type); u16out(1)
            output.write(byteArrayOf(0,0,0,0)); u16out(address.address.size); output.write(address.address)
        }
        return DnsRawProtocol.Result.Answer(0,output.toByteArray())
    }
}
