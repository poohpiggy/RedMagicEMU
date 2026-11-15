package app.aemu.core

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

class TargetDnsAdmissionTest {
    @Test fun saturationRejectsBeforeEnteringGuestAndLaterRecovers() {
        val entered=CountDownLatch(32)
        val release=CountDownLatch(1)
        val begins=AtomicInteger()
        val network=TargetDnsResolver.HostNetwork(Any(), 1)
        val networks=object:TargetDnsResolver.HostNetworks {
            override fun snapshot()=network
            override fun isCurrent(snapshot:TargetDnsResolver.HostNetwork)=snapshot==network
        }
        val guest=object:TargetDnsResolver.GuestNetwork {
            override fun begin(token:Long,id:Long):Boolean { begins.incrementAndGet(); entered.countDown(); release.await(); return true }
            override fun finish(token:Long)=true
        }
        val resolver=TargetDnsResolver(networks,guest) { _,_,_-> DnsRawProtocol.Result.Answer(0,byteArrayOf(7)) }
        val executor=Executors.newFixedThreadPool(33)
        try {
            val pending=(0 until 32).map { executor.submit<DnsRawProtocol.Result> { resolver.query(101,byteArrayOf(1),0) } }
            assertTrue("32 admitted requests reach begin",entered.await(5,TimeUnit.SECONDS))
            val excess=executor.submit<DnsRawProtocol.Result> { resolver.query(101,byteArrayOf(1),0) }
            val result=excess.get(1,TimeUnit.SECONDS)
            assertTrue(result is DnsRawProtocol.Result.Error)
            assertEquals(16,(result as DnsRawProtocol.Result.Error).errno)
            assertEquals("excess does not enter guest channel",32,begins.get())
            release.countDown()
            pending.forEach { assertTrue(it.get(5,TimeUnit.SECONDS) is DnsRawProtocol.Result.Answer) }
            assertTrue(resolver.query(101,byteArrayOf(1),0) is DnsRawProtocol.Result.Answer)
        } finally { release.countDown(); executor.shutdownNow(); assertTrue(executor.awaitTermination(5,TimeUnit.SECONDS)) }
    }
    @Test fun retirementRejectsNewWorkAndLateAnswerWhileFinishingLease() {
        val rawEntered=CountDownLatch(1); val release=CountDownLatch(1); val finishes=AtomicInteger()
        val network=TargetDnsResolver.HostNetwork(Any(),1)
        val networks=object:TargetDnsResolver.HostNetworks {
            override fun snapshot()=network
            override fun isCurrent(snapshot:TargetDnsResolver.HostNetwork)=true
        }
        val guest=object:TargetDnsResolver.GuestNetwork {
            override fun begin(token:Long,id:Long)=true
            override fun finish(token:Long):Boolean { finishes.incrementAndGet(); return true }
        }
        val resolver=TargetDnsResolver(networks,guest) { _,_,_->
            rawEntered.countDown(); release.await(); DnsRawProtocol.Result.Answer(0,byteArrayOf(7))
        }
        val executor=Executors.newSingleThreadExecutor()
        try {
            val request=executor.submit<DnsRawProtocol.Result> { resolver.query(101,byteArrayOf(1),0) }
            assertTrue(rawEntered.await(5,TimeUnit.SECONDS)); resolver.close(); resolver.close()
            val rejected=resolver.query(101,byteArrayOf(1),0)
            assertEquals(64,(rejected as DnsRawProtocol.Result.Error).errno)
            release.countDown()
            assertEquals(64,(request.get(5,TimeUnit.SECONDS) as DnsRawProtocol.Result.Error).errno)
            assertEquals(1,finishes.get())
        } finally { release.countDown(); executor.shutdownNow(); assertTrue(executor.awaitTermination(5,TimeUnit.SECONDS)) }
    }

    @Test fun failedBeginReleasesAdmission() {
        val network=TargetDnsResolver.HostNetwork(Any(),1)
        val networks=object:TargetDnsResolver.HostNetworks {
            override fun snapshot()=network
            override fun isCurrent(snapshot:TargetDnsResolver.HostNetwork)=true
        }
        val guest=object:TargetDnsResolver.GuestNetwork {
            override fun begin(token:Long,id:Long):Boolean { throw java.io.IOException("verifier died") }
            override fun finish(token:Long)=throw AssertionError("no lease")
        }
        val resolver=TargetDnsResolver(networks,guest) { _,_,_-> throw AssertionError("no query") }
        repeat(64) { assertEquals(64,(resolver.query(101,byteArrayOf(1),0) as DnsRawProtocol.Result.Error).errno) }
    }

    @Test fun retirementWhileBeginBlockedFinishesLeaseWithoutStartingQuery() {
        val entered=CountDownLatch(1); val release=CountDownLatch(1); val finishes=AtomicInteger(); val raws=AtomicInteger()
        val network=TargetDnsResolver.HostNetwork(Any(),1)
        val networks=object:TargetDnsResolver.HostNetworks {
            override fun snapshot()=network
            override fun isCurrent(snapshot:TargetDnsResolver.HostNetwork)=true
        }
        val guest=object:TargetDnsResolver.GuestNetwork {
            override fun begin(token:Long,id:Long):Boolean { entered.countDown(); release.await(); return true }
            override fun finish(token:Long):Boolean { finishes.incrementAndGet(); return true }
        }
        val resolver=TargetDnsResolver(networks,guest) { _,_,_-> raws.incrementAndGet(); DnsRawProtocol.Result.Answer(0,byteArrayOf(7)) }
        val executor=Executors.newSingleThreadExecutor()
        try {
            val request=executor.submit<DnsRawProtocol.Result> { resolver.query(101,byteArrayOf(1),0) }
            assertTrue(entered.await(5,TimeUnit.SECONDS)); resolver.close(); release.countDown()
            assertEquals(64,(request.get(5,TimeUnit.SECONDS) as DnsRawProtocol.Result.Error).errno)
            assertEquals(1,finishes.get()); assertEquals(0,raws.get())
        } finally { release.countDown(); executor.shutdownNow(); assertTrue(executor.awaitTermination(5,TimeUnit.SECONDS)) }
    }
    @Test fun retiredResolverCannotReturnLateAnswerAfterNewResolverSucceeds() {
        val entered=CountDownLatch(1); val release=CountDownLatch(1); val oldFinished=AtomicInteger()
        val network=TargetDnsResolver.HostNetwork(Any(),1)
        val networks=object:TargetDnsResolver.HostNetworks {
            override fun snapshot()=network
            override fun isCurrent(snapshot:TargetDnsResolver.HostNetwork)=true
        }
        fun guest(finished:AtomicInteger)=object:TargetDnsResolver.GuestNetwork {
            override fun begin(token:Long,id:Long)=true
            override fun finish(token:Long):Boolean { finished.incrementAndGet(); return true }
        }
        val old=TargetDnsResolver(networks,guest(oldFinished)) { _,_,_-> entered.countDown(); release.await(); DnsRawProtocol.Result.Answer(0,byteArrayOf(1)) }
        val pool=Executors.newSingleThreadExecutor()
        try {
            val late=pool.submit<DnsRawProtocol.Result>{old.query(101,byteArrayOf(1),0)}
            assertTrue(entered.await(5,TimeUnit.SECONDS)); old.close()
            val newFinished=AtomicInteger(); val replacement=TargetDnsResolver(networks,guest(newFinished)) { _,_,_-> DnsRawProtocol.Result.Answer(0,byteArrayOf(2)) }
            assertArrayEquals(byteArrayOf(2),(replacement.query(102,byteArrayOf(1),0) as DnsRawProtocol.Result.Answer).packet)
            release.countDown(); assertEquals(64,(late.get(5,TimeUnit.SECONDS) as DnsRawProtocol.Result.Error).errno)
            assertEquals(1,oldFinished.get()); assertEquals(1,newFinished.get())
            assertTrue(replacement.query(102,byteArrayOf(1),0) is DnsRawProtocol.Result.Answer)
        } finally { release.countDown(); pool.shutdownNow(); assertTrue(pool.awaitTermination(5,TimeUnit.SECONDS)) }
    }

    @Test fun fatalRawErrorStillFinishesLeaseAndReleasesAdmission() {
        val finished=AtomicInteger(); val network=TargetDnsResolver.HostNetwork(Any(),1)
        val networks=object:TargetDnsResolver.HostNetworks { override fun snapshot()=network; override fun isCurrent(snapshot:TargetDnsResolver.HostNetwork)=true }
        val guest=object:TargetDnsResolver.GuestNetwork { override fun begin(token:Long,id:Long)=true; override fun finish(token:Long):Boolean { finished.incrementAndGet(); return true } }
        val resolver=TargetDnsResolver(networks,guest) { _,_,_-> throw AssertionError("fatal") }
        repeat(40) { var threw=false; try { resolver.query(101,byteArrayOf(1),0) } catch (error:AssertionError) { assertEquals("fatal",error.message); threw=true }; assertTrue(threw) }
        assertEquals(40,finished.get())
    }
}
