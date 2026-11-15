package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.Base64

class TargetRawDnsDispatchTest {
    private val query=ByteArray(12).also { it[2]=1; it[5]=1 }
    private fun command()=listOf("resnsend","101","0",Base64.getEncoder().encodeToString(query))
    @Test fun unavailableResolverKeepsExistingUnsupportedContract() {
        assertArrayEquals(unsupportedTargetRawDns(36,command()),targetRawDnsReply(36,command(),true,null))
        assertNull(targetRawDnsReply(35,command(),true,null))
        assertNull(targetRawDnsReply(36,listOf("resnsend"),true,null))
    }
    @Test fun validTargetQueryReachesResolverWithExactArguments() {
        var calls=0
        val response=targetRawDnsReply(36,command(),true) { id, packet, flags ->
            calls++; assertEquals(101,id); assertArrayEquals(query,packet); assertEquals(0,flags)
            DnsRawProtocol.Result.Answer(0,ByteArray(12))
        }
        assertEquals(1,calls); assertEquals(20,response!!.size)
    }
    @Test fun malformedOrUnterminatedCannotReachResolver() {
        var calls=0
        val resolver:(Long,ByteArray,Int)->DnsRawProtocol.Result = { _,_,_-> calls++; DnsRawProtocol.Result.Answer(0,ByteArray(12)) }
        assertArrayEquals(DnsRawProtocol.error(90),targetRawDnsReply(36,command(),false,resolver))
        assertArrayEquals(DnsRawProtocol.error(22),targetRawDnsReply(36,listOf("resnsend","invalid","0","!"),true,resolver))
        assertEquals(0,calls)
    }
    @Test fun oldApiNeverCallsTargetResolver() {
        var calls=0
        assertNull(targetRawDnsReply(35,command(),true) { _,_,_-> calls++; DnsRawProtocol.Result.Error(5) })
        assertEquals(0,calls)
    }
}
