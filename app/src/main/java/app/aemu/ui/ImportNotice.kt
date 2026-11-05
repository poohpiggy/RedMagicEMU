package app.aemu.ui

/** The upstream generic engine warning is obsolete for this fixed ARM64 target. */
internal fun visibleImportWarnings(warnings: List<String>): List<String> = warnings.filterNot {
    it == "64-bit firmware: the native arm64 engine is tuned for Android 6.0; other versions are experimental."
}
