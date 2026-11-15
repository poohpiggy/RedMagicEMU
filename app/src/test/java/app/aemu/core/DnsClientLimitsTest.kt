package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.io.Closeable
import java.io.InputStream
import java.net.SocketTimeoutException
import java.util.concurrent.atomic.AtomicInteger

class DnsClientLimitsTest {
 @Test fun limitAndStopCloseOldClientsButKeepSlotsUntilWorkersExit() {
  val admission=DnsClientAdmission(2); val oldClosed=AtomicInteger(); val newClosed=AtomicInteger()
  val old1=Closeable { oldClosed.incrementAndGet() }; val old2=Closeable { oldClosed.incrementAndGet() }; val fresh=Closeable { newClosed.incrementAndGet() }
  val g1=admission.start(); val a=admission.tryAdmit(old1,g1)!!; val b=admission.tryAdmit(old2,g1)!!
  val rejected=AtomicInteger(); assertNull(admission.tryAdmit(Closeable { rejected.incrementAndGet() },g1)); assertEquals(1,rejected.get()); admission.stop(); assertEquals(2,oldClosed.get()); assertNull(admission.tryAdmit(Closeable {},g1))
  val g2=admission.start(); assertNull(admission.tryAdmit(Closeable {},g1)); assertNull(admission.tryAdmit(Closeable {},g2)); a.close(); a.close()
  val c=admission.tryAdmit(fresh,g2); assertNotNull(c); assertNull(admission.tryAdmit(Closeable {},g2))
  b.close(); admission.stop(); assertEquals(1,newClosed.get()); c!!.close()
  val g3=admission.start(); assertNotNull(admission.tryAdmit(Closeable {},g3)); admission.stop()
 }
 @Test fun validCommandIsCompleteAndDeadlineIsApplied() {
  val timeouts=ArrayList<Int>(); val command="resnsend 101 0 AA==\u0000".byteInputStream()
  val result=DnsCommandReader.read(command,1000,{timeouts.add(it)},{0L})
  assertTrue(result.terminated); assertEquals("resnsend 101 0 AA==",result.bytes.toString(Charsets.UTF_8)); assertTrue(timeouts.isNotEmpty()); assertTrue(timeouts.all { it in 1..1000 })
 }
 @Test fun slowDripCannotExtendOverallDeadline() {
  var nanos=0L; val timeouts=ArrayList<Int>(); val stream=object:InputStream(){ override fun read():Int { nanos+=400_000_000L; return 65 } }
  try { DnsCommandReader.read(stream,1000,{timeouts.add(it)},{nanos}); fail("deadline required") } catch (_:SocketTimeoutException) {}
  assertEquals(listOf(1000,600,200),timeouts)
 }
 @Test fun oversizedInputNeverClaimsTerminated() {
  val result=DnsCommandReader.read(ByteArray(1024){65}.inputStream(),1000,{}, {0L})
  assertFalse(result.terminated); assertEquals(1024,result.bytes.size)
 }
 @Test fun stalledSocketReadTimesOutAndSlotCanBeReleased() {
  val admission=DnsClientAdmission(1); val generation=admission.start()
  java.net.ServerSocket(0).use { server ->
   java.net.Socket("127.0.0.1",server.localPort).use { peer ->
    server.accept().use { accepted ->
     val lease=admission.tryAdmit(accepted,generation)!!
     val start=System.nanoTime()
     try { DnsCommandReader.read(accepted.getInputStream(),150,{accepted.soTimeout=it}); fail("read must time out") }
     catch (_:SocketTimeoutException) { }
     finally { lease.close() }
     val elapsed=java.util.concurrent.TimeUnit.NANOSECONDS.toMillis(System.nanoTime()-start)
     assertTrue("deadline bounded $elapsed",elapsed < 2500)
     assertNotNull(admission.tryAdmit(Closeable {},generation)); admission.stop()
    }
   }
  }
 }
}
