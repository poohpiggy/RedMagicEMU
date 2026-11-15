package app.aemu.core

import org.junit.Assert.*
import org.junit.Test

class TargetDnsResolverTest {
    private class Networks : TargetDnsResolver.HostNetworks {
        var current = TargetDnsResolver.HostNetwork(Any(), 1)
        var available = true
        override fun snapshot(): TargetDnsResolver.HostNetwork? = if (available) current else null
        override fun isCurrent(snapshot: TargetDnsResolver.HostNetwork): Boolean = available && snapshot == current
    }
    private class Guest : TargetDnsResolver.GuestNetwork {
        var begin = true; var end = true; var opened = 0; var closed = 0
        override fun begin(token: Long, id: Long): Boolean { opened++; return begin }
        override fun finish(token: Long): Boolean { closed++; return end }
    }
    @Test fun knownConnectedNetworkReturnsActualAnswer() {
        val n=Networks(); val g=Guest(); val answer = byteArrayOf(1,2,3)
        val r=TargetDnsResolver(n,g) { _,_,_ -> DnsRawProtocol.Result.Answer(0,answer) }
        assertSame(answer, (r.query(101, byteArrayOf(1), 0) as DnsRawProtocol.Result.Answer).packet)
        assertEquals(1,g.opened); assertEquals(1,g.closed)
    }
    @Test fun otherAndDefaultNetworkAreCheckedByGuest() {
        val n=Networks(); val g=Guest().apply { begin=false }; var calls=0
        val r=TargetDnsResolver(n,g) { _,_,_ -> calls++; DnsRawProtocol.Result.Answer(0, byteArrayOf()) }
        assertTrue(r.query(102, byteArrayOf(1), 0) is DnsRawProtocol.Result.Error)
        assertEquals(0,calls); assertEquals(0,g.closed)
    }
    @Test fun lossOrHostSwitchDuringResolutionRejectsAnswer() {
        val n=Networks(); val g=Guest()
        val changedHost=TargetDnsResolver(n,g) { _,_,_ -> n.current=TargetDnsResolver.HostNetwork(Any(),2); DnsRawProtocol.Result.Answer(0,byteArrayOf(1)) }
        assertTrue(changedHost.query(101, byteArrayOf(1), 0) is DnsRawProtocol.Result.Error)
        val lostGuest=TargetDnsResolver(n,g) { _,_,_ -> g.end=false; DnsRawProtocol.Result.Answer(0,byteArrayOf(1)) }
        assertTrue(lostGuest.query(101, byteArrayOf(1), 0) is DnsRawProtocol.Result.Error)
        assertEquals(2,g.closed)
    }
    @Test fun missingHostAndResolverFailureDoNotReportSuccess() {
        val n=Networks().apply { available=false }; val g=Guest(); var calls=0
        val r=TargetDnsResolver(n,g) { _,_,_ -> calls++; throw java.io.IOException("offline") }
        assertTrue(r.query(101,byteArrayOf(1),0) is DnsRawProtocol.Result.Error)
        assertEquals(0,g.opened); assertEquals(0,calls)
        n.available=true
        assertTrue(r.query(101,byteArrayOf(1),0) is DnsRawProtocol.Result.Error)
        assertEquals(1,g.closed)
    }

    @Test fun verifierProcessDeathRejectsLateAnswersAndANewSessionCanRecover() {
        val networks=Networks(); var alive=true
        val deadGuest=object : TargetDnsResolver.GuestNetwork {
            override fun begin(token: Long, id: Long): Boolean {
                if (!alive) throw java.io.IOException("verifier exited")
                return true
            }
            override fun finish(token: Long): Boolean {
                if (!alive) throw java.io.IOException("verifier exited")
                return true
            }
        }
        val old=TargetDnsResolver(networks,deadGuest) { _,_,_ ->
            alive=false
            DnsRawProtocol.Result.Answer(0,byteArrayOf(7))
        }
        assertTrue(old.query(101,byteArrayOf(1),0) is DnsRawProtocol.Result.Error)
        assertTrue(old.query(101,byteArrayOf(1),0) is DnsRawProtocol.Result.Error)
        val replacement=Guest()
        val fresh=TargetDnsResolver(networks,replacement) { _,_,_ -> DnsRawProtocol.Result.Answer(0,byteArrayOf(8)) }
        assertTrue(fresh.query(101,byteArrayOf(1),0) is DnsRawProtocol.Result.Answer)
        assertEquals(1,replacement.opened); assertEquals(1,replacement.closed)
    }
}
