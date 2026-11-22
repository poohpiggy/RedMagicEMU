package app.aemu.core

import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.telephony.CellInfoLte
import android.telephony.TelephonyManager

internal data class HostLteObservation(
    val observedMs: Long,
    val mcc: String,
    val mnc: String,
    val ci: Int,
    val tac: Int,
    val pci: Int,
    val earfcn: Int,
    val rsrp: Int,
) {
    fun isValid(nowMs: Long): Boolean =
        observedMs > 0 && observedMs <= nowMs && nowMs - observedMs <= 30_000L &&
            mcc.length == 3 && mcc.all { it in '0'..'9' } &&
            mnc.length in 2..3 && mnc.all { it in '0'..'9' } &&
            ci in 0..268_435_455 && tac in 0..65_535 && pci in 0..503 &&
            earfcn in 0..262_143 && (rsrp in -140..-44 || rsrp == Int.MAX_VALUE)

    fun encode(): String = "1 $observedMs $mcc $mnc $ci $tac $pci $earfcn $rsrp"
}

/** Reads the host's serving LTE observation only during an enabled Guest location request. */
internal class HostLteLocation(
    private val context: Context,
    private val log: (String) -> Unit,
    private val channel: () -> RadioHostChannel?,
) {
    private var thread: HandlerThread? = null
    private var handler: Handler? = null
    private var active = false
    private var generation = 0L
    private var last: HostLteObservation? = null

    @Synchronized fun setActive(value: Boolean) {
        if (active == value) return
        active = value
        val session = ++generation
        if (value && handler == null) {
            val owner = HandlerThread("host-lte-location").also { it.start() }
            thread = owner
            handler = Handler(owner.looper)
        }
        handler?.removeCallbacksAndMessages(null)
        if (value) handler?.post { poll(session) }
        else handler?.post { deliver(session, null) }
    }

    @Synchronized fun stop() {
        setActive(false)
        thread?.quitSafely()
        thread = null
        handler = null
        last = null
    }

    @Synchronized private fun current(session: Long): Boolean = active && session == generation

    @Suppress("DEPRECATION", "MissingPermission")
    private fun poll(session: Long) {
        if (!current(session)) return
        val radio = context.getSystemService(TelephonyManager::class.java)
        val observation = try {
            radio?.allCellInfo.orEmpty().asSequence().filterIsInstance<CellInfoLte>()
                .filter { it.isRegistered }.mapNotNull { cell ->
                    val id = cell.cellIdentity
                    HostLteObservation(
                        if (Build.VERSION.SDK_INT >= 30) cell.timestampMillis else cell.timeStamp / 1_000_000L,
                        id.mccString ?: return@mapNotNull null, id.mncString ?: return@mapNotNull null,
                        id.ci, id.tac, id.pci, id.earfcn, cell.cellSignalStrength.rsrp,
                    ).takeIf { it.isValid(SystemClock.elapsedRealtime()) }
                }.maxByOrNull { it.observedMs }
        } catch (_: SecurityException) {
            null
        } catch (_: UnsupportedOperationException) {
            null
        } catch (_: IllegalStateException) {
            null
        }
        deliver(session, observation)
        synchronized(this) {
            if (active && generation == session) handler?.postDelayed({ poll(session) }, 8_000L)
        }
    }

    private fun deliver(session: Long, observation: HostLteObservation?) {
        val target = synchronized(this) {
            if (generation != session || (active && last == observation)) return
            channel()
        }
        val payload = observation?.encode() ?: "0"
        if (target?.cellObservation(payload) == RadioSubmitResult.SUBMITTED) synchronized(this) {
            if (generation == session) {
                if (last == null && observation != null) log("location: serving LTE observation accepted by Guest Radio")
                last = observation
            }
        }
    }
}
