package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.net.InetAddress

class AddressDnsReplyTest {
    // ID=0x1234, standard recursive IN A query for example.test.
    private val query=byteArrayOf(0x12,0x34,0x01,0,0,1,0,0,0,0,0,0,7,101,120,97,109,112,108,101,4,116,101,115,116,0,0,1,0,1)
    private val ip=InetAddress.getByAddress(byteArrayOf(1,2,3,4))
    @Test fun replyUsesOnlyRealNetworkResolverAddresses() {
        var asked=""
        val result=AddressDnsReply.query(query,0) { name -> asked=name; listOf(ip) } as DnsRawProtocol.Result.Answer
        assertEquals("example.test.",asked); assertEquals(0,result.rcode)
        assertEquals(0x12,result.packet[0].toInt()); assertEquals(0x34,result.packet[1].toInt())
        assertEquals(1,result.packet[7].toInt()); assertArrayEquals(ip.address,result.packet.takeLast(4).toByteArray())
    }
    @Test fun unsupportedTypesAndPolicyFlagsNeverPretendSuccess() {
        val mx=query.copyOf().also { it[it.size-3]=15 }
        var called=false
        assertTrue(AddressDnsReply.query(mx,0) { called=true; listOf(ip) } is DnsRawProtocol.Result.Error)
        assertFalse(called)
        assertTrue(AddressDnsReply.query(query,1) { called=true; listOf(ip) } is DnsRawProtocol.Result.Error)
        assertFalse(called)
    }
    @Test fun malformedQueriesAndFailedResolutionReturnErrors() {
        val compressed=query.copyOf().also { it[12]=0xc0.toByte() }
        assertTrue(AddressDnsReply.query(compressed,0) { throw AssertionError() } is DnsRawProtocol.Result.Error)
        assertTrue(AddressDnsReply.query(query,0) { throw java.net.UnknownHostException() } is DnsRawProtocol.Result.Error)
    }
    @Test fun noMatchingFamilyCannotClaimAuthoritativeNodata() {
        val result=AddressDnsReply.query(query,0) { listOf(InetAddress.getByAddress(ByteArray(16))) }
        assertTrue("Unrelated address family does not prove DNS NODATA", result is DnsRawProtocol.Result.Error)
    }
}
