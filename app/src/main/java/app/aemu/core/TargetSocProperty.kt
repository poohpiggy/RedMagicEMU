package app.aemu.core

internal fun PropArea.applyTargetBootProperties() {
    if (get("ro.soc.model").isNullOrEmpty()) {
        check(put("ro.soc.model", "SM8850")) { "Could not set target SoC model" }
    }
    // The target firmware sets this in qspa_default.rc's on-init action.
    val qspa = get("ro.boot.vendor.qspa")
    if ((qspa.isNullOrEmpty() || qspa == "default") && get("ro.boot.vendor.qspa.modem") == null) {
        check(put("ro.boot.vendor.qspa.modem", "enabled")) { "Could not set target modem property" }
    }
}
