package app.aemu.core

import android.content.Context
import android.content.SharedPreferences

/** User preference for the next target guest boot, separate from observed network state. */
internal enum class TargetNetworkMode(val stored: String) {
    ETHERNET("ethernet"), WIFI("wifi"), LTE("lte");

    fun prepareWifi(runtimeReady: Boolean, target: Boolean): Boolean = this == WIFI && runtimeReady && target
    fun prepareLte(runtimeReady: Boolean, target: Boolean, radio: Boolean): Boolean =
        this == LTE && runtimeReady && target && radio

    companion object {
        private const val PREFS = "target_network_modes"

        fun parse(value: String?): TargetNetworkMode = entries.firstOrNull { it.stored == value } ?: ETHERNET

        fun key(id: String): String {
            require(ImageId.valid(id)) { "Invalid instance ID" }
            return "mode_$id"
        }

        fun read(context: Context, id: String): TargetNetworkMode =
            read(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE), id)

        fun write(context: Context, id: String, mode: TargetNetworkMode): Boolean =
            write(context.getSharedPreferences(PREFS, Context.MODE_PRIVATE), id, mode)

        internal fun read(preferences: SharedPreferences, id: String): TargetNetworkMode =
            parse(preferences.all[key(id)] as? String)

        internal fun write(preferences: SharedPreferences, id: String, mode: TargetNetworkMode): Boolean =
            preferences.edit().putString(key(id), mode.stored).commit()
    }
}
