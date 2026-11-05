package app.aemu.core

internal fun requireSystemBoot(recovery: Boolean) {
    check(!recovery) { "Recovery mode is not supported" }
}
