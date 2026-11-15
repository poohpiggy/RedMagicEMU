package app.aemu.core

/** Select the multi-rate composer only for the supported NX809J firmware. */
internal object TargetRefreshComposer {
    const val FILE = "vendor#lib64#hw#hwcomposer.default.so"
    private const val ASSET = "engines/redmagic/hwcomposer-refresh.so"

    fun select(original: String, api: Int, model: String, abi: String, engine: Engine): String =
        if (api == 36 && model == "NX809J" && abi == "arm64-v8a" && engine == Engine.N64) ASSET else original
}
