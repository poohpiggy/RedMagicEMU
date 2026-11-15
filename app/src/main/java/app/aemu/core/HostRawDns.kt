package app.aemu.core

import android.annotation.TargetApi
import android.net.DnsResolver
import android.net.Network
import android.os.Build
import android.os.CancellationSignal
import android.system.ErrnoException
import java.util.concurrent.Executor
import java.util.concurrent.Semaphore

/** Sends a real DNS query through the host's default network and host DNS policy. */
internal object HostRawDns {
    private const val EIO = 5
    private val inFlight = Semaphore(32)

    fun query(network: Network, packet: ByteArray, flags: Int): DnsRawProtocol.Result {
        // Android 8/9 lack rawQuery: handle only ordinary A/AAAA via the host's network resolver.
        if (Build.VERSION.SDK_INT < 29) return DnsRawAwaiter.await(inFlight, 5000) { complete ->
            complete(AddressDnsReply.query(packet, flags) { network.getAllByName(it).toList() })
            val cancel: () -> Unit = { }
            cancel
        }
        return Api29.query(network, packet, flags)
    }

    @TargetApi(29)
    private object Api29 {
        private val direct = Executor { it.run() }

        fun query(network: Network, packet: ByteArray, flags: Int): DnsRawProtocol.Result {
            val cancel = CancellationSignal()
            return DnsRawAwaiter.await(inFlight, 5000) { complete ->
                // This is the actual host default captured and checked by TargetHostNetworks.
                try {
                    DnsResolver.getInstance().rawQuery(network, packet, flags, direct, cancel,
                        object : DnsResolver.Callback<ByteArray> {
                            override fun onAnswer(answer: ByteArray, rcode: Int) {
                                complete(DnsRawProtocol.Result.Answer(rcode, answer))
                            }
                            override fun onError(error: DnsResolver.DnsException) {
                                val errno = (error.cause as? ErrnoException)?.errno ?: EIO
                                complete(DnsRawProtocol.Result.Error(errno))
                            }
                        })
                } catch (failure: Throwable) {
                    cancel.cancel()
                    throw failure
                }
                { cancel.cancel() }
            }
        }
    }
}
