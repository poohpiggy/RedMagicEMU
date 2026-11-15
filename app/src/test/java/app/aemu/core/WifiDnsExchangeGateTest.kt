package app.aemu.core
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class WifiDnsExchangeGateTest {
 @Test fun closeWakesQueuedWorkAndRejectsLateReplyWithoutWaitingForBlockedIo() {
  val gate=WifiDnsExchangeGate(); val entered=CountDownLatch(1); val release=CountDownLatch(1); val queuedEntered=AtomicInteger(); val closes=AtomicInteger()
  val pool=Executors.newFixedThreadPool(2)
  try {
   val active=pool.submit<Int>{gate.run(-1,-2) { entered.countDown(); release.await(); 7 }}
   assertTrue(entered.await(5,TimeUnit.SECONDS))
   val queued=pool.submit<Int>{gate.run(-1,-2) { queuedEntered.incrementAndGet(); 8 }}
   gate.close { closes.incrementAndGet() }
   assertEquals(-1,queued.get(1,TimeUnit.SECONDS).toLong()); assertEquals(0,queuedEntered.get())
   assertEquals(1,closes.get()); gate.close { closes.incrementAndGet() }; assertEquals(1,closes.get())
   release.countDown(); assertEquals(-2,active.get(5,TimeUnit.SECONDS).toLong())
   assertEquals(-1,gate.run(-1,-2) { 9 }.toLong())
  } finally { release.countDown(); pool.shutdownNow(); assertTrue(pool.awaitTermination(5,TimeUnit.SECONDS)) }
 }
 @Test fun exceptionReleasesSerialAdmission() {
  val gate=WifiDnsExchangeGate()
  try { gate.run(-1,-2) { throw IllegalStateException("test") }; fail() } catch (_:IllegalStateException) {}
  assertEquals(8,gate.run(-1,-2) { 8 }.toLong())
 }
 @Test fun retiredGateCannotPublishLateSuccessOrCloseReplacement() {
  val old=WifiDnsExchangeGate(); val fresh=WifiDnsExchangeGate(); val entered=CountDownLatch(1); val release=CountDownLatch(1); val pool=Executors.newSingleThreadExecutor()
  try {
   val oldReply=pool.submit<Int> { old.run(-1,-2) { entered.countDown(); release.await(); 42 } }
   assertTrue(entered.await(5,TimeUnit.SECONDS)); old.close { }
   assertEquals(7,fresh.run(-1,-2) { 7 }.toLong())
   release.countDown(); assertEquals(-2,oldReply.get(5,TimeUnit.SECONDS).toLong())
   assertEquals(8,fresh.run(-1,-2) { 8 }.toLong())
  } finally { release.countDown(); pool.shutdownNow(); assertTrue(pool.awaitTermination(5,TimeUnit.SECONDS)) }
 }
}
