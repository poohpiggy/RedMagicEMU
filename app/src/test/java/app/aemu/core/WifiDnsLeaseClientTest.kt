package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.nio.ByteBuffer

class WifiDnsLeaseClientTest {
    @Test fun sendsUnsignedNetIdAndTokenToAuthenticatedTransport() {
        val requests=mutableListOf<ByteArray>()
        val client=WifiDnsLeaseClient { data -> requests.add(data); RadioSubmitResult.SUBMITTED }
        assertTrue(client.begin(7,2147483749L)); assertTrue(client.finish(7))
        val a=ByteBuffer.wrap(requests[0]); assertEquals(1,a.get().toInt()); assertEquals(7L,a.long); assertEquals(2147483749L,Integer.toUnsignedLong(a.int))
        val b=ByteBuffer.wrap(requests[1]); assertEquals(2,b.get().toInt()); assertEquals(7L,b.long); assertEquals(0,b.int)
    }
    @Test fun denialIsNotSuccessAndTransportFailureThrows() {
        val denied=WifiDnsLeaseClient { RadioSubmitResult.REJECTED }; assertFalse(denied.begin(1,101))
        val lost=WifiDnsLeaseClient { RadioSubmitResult.UNCERTAIN }
        try { lost.begin(1,101); fail("uncertain channel accepted") } catch (_: java.io.IOException) { }
    }
    @Test fun requestsARealGuestReevaluationWhenHostAvailabilityChanges() {
        val frames=mutableListOf<ByteArray>()
        val client=WifiDnsLeaseClient { data -> frames.add(data); RadioSubmitResult.SUBMITTED }
        assertTrue(client.report(false)); assertTrue(client.report(true))
        val a=ByteBuffer.wrap(frames[0]); assertEquals(3,a.get().toInt()); assertTrue(a.long>0); assertEquals(0,a.int)
        val b=ByteBuffer.wrap(frames[1]); assertEquals(3,b.get().toInt()); assertTrue(b.long>0); assertEquals(1,b.int)
    }
    @Test fun uncertainExchangeRetiresVerifierButPreAttachUnavailableDoesNot() {
        var retirements=0
        val uncertain=WifiDnsLeaseClient({ retirements++ }) { RadioSubmitResult.UNCERTAIN }
        try { uncertain.begin(3,101); fail("uncertain exchange accepted") } catch (_: java.io.IOException) { }
        assertEquals(1,retirements)
        val unavailable=WifiDnsLeaseClient({ retirements++ }) { RadioSubmitResult.UNAVAILABLE }
        try { unavailable.begin(4,101); fail("unavailable exchange accepted") } catch (_: java.io.IOException) { }
        assertEquals(1,retirements)
    }
}
