package app.aemu.core

import java.io.File

/** Selects the Android 16 target's AIDL Radio. */
internal object TargetRadioLaunch {
    internal enum class Backend { DISABLED, AIDL }
    private const val JAR = "/system/framework/redmagic-radio.jar"

    fun select(api: Int, model: String, abi: String, engine: Engine, enabled: Boolean): Backend = when {
        !enabled -> Backend.DISABLED
        api == 36 && model == "NX809J" && abi == "arm64-v8a" && engine == Engine.N64 -> Backend.AIDL
        else -> Backend.DISABLED
    }

    fun routeEvent(backend: Backend, aidl: () -> RadioSubmitResult): RadioSubmitResult = when (backend) {
        Backend.AIDL -> aidl()
        Backend.DISABLED -> RadioSubmitResult.UNAVAILABLE
    }

    /** The synthetic AIDL service exposes one modem; do not inherit the ROM's dual-modem mode. */
    fun applyBootProperties(backend: Backend, overrides: MutableMap<String, String>) {
        if (backend == Backend.AIDL) overrides["persist.radio.multisim.config"] = ""
    }

    fun service(root: File, lteData: Boolean = false): GuestService {
        check(File(root, JAR.removePrefix("/")).isFile) { "Radio service JAR missing: $JAR" }
        val args = listOf("/system/bin/app_process", "-Djava.class.path=$JAR",
            "/system/bin", "app.aemu.stub.RadioServiceMain", "--experimental-slot1") +
            if (lteData) listOf("--lte-data") else emptyList()
        return GuestService("aemu-radio", args)
    }

    fun service(root: File, socketName: String, hostPid: Int, hostUid: Int, epoch: Long, lteData: Boolean = false): GuestService {
        require(socketName.matches(Regex("[A-Za-z0-9_.-]{1,80}")) && hostPid > 0 && hostUid >= 0 && epoch > 0)
        val base = service(root, lteData)
        return base.copy(argv = base.argv + listOf("--event-socket", socketName, hostPid.toString(), hostUid.toString(), epoch.toString()))
    }

    fun requireServiceOrder(plan: List<GuestService>) {
        val manager = plan.indexOfFirst { it.name == "servicemanager" && !it.onDemand }
        val zygote = plan.indexOfFirst { it.name == "zygote" && !it.onDemand }
        check(manager >= 0 && zygote > manager) { "Radio needs servicemanager before zygote" }
    }
}
