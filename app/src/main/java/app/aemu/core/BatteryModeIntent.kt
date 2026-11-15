package app.aemu.core

import android.content.Context
import android.content.Intent

internal data class BatteryModeRequest(val host: Boolean, val custom: GuestBatteryState? = null)

/** One-shot delivery from the instance settings sheet to a running VM; never written to instance settings. */
internal object BatteryModeIntent {
    val action = app.aemu.BuildConfig.APPLICATION_ID + ".BATTERY_MODE"
    val queryAction = app.aemu.BuildConfig.APPLICATION_ID + ".BATTERY_QUERY"
    val snapshotAction = app.aemu.BuildConfig.APPLICATION_ID + ".BATTERY_SNAPSHOT"
    private const val HOST = "host"
    private const val LEVEL = "level"
    private const val SOURCE = "source"

    fun publish(context: Context, host: Boolean, custom: GuestBatteryState?) {
        if (!host && custom == null) return
        val intent = Intent(action).setPackage(context.packageName).putExtra(HOST, host)
        if (!host) intent.putExtra(LEVEL, custom!!.level).putExtra(SOURCE, custom.source)
        context.sendBroadcast(intent)
    }

    fun request(context: Context) {
        context.sendBroadcast(Intent(queryAction).setPackage(context.packageName))
    }

    fun publishSnapshot(context: Context, state: GuestBatteryState) {
        context.sendBroadcast(Intent(snapshotAction).setPackage(context.packageName)
            .putExtra(LEVEL, state.level).putExtra(SOURCE, state.source))
    }

    fun readSnapshot(intent: Intent): GuestBatteryState? {
        if (intent.action != snapshotAction) return null
        val level = intent.getIntExtra(LEVEL, -1)
        val source = intent.getIntExtra(SOURCE, -1)
        if (level !in 0..100 || source !in setOf(
                GuestBatteryControl.NONE,
                GuestBatteryControl.AC,
                GuestBatteryControl.USB,
                GuestBatteryControl.WIRELESS,
            )) return null
        return GuestBatteryState(level, source)
    }

    fun read(intent: Intent): BatteryModeRequest? {
        if (intent.action != action) return null
        val host = intent.getBooleanExtra(HOST, true)
        if (host) return BatteryModeRequest(true)
        val level = intent.getIntExtra(LEVEL, -1)
        val source = intent.getIntExtra(SOURCE, -1)
        if (level !in 0..100 || source !in setOf(
                GuestBatteryControl.NONE,
                GuestBatteryControl.AC,
                GuestBatteryControl.USB,
                GuestBatteryControl.WIRELESS,
            )) return null
        return BatteryModeRequest(false, GuestBatteryState(level, source))
    }
}
