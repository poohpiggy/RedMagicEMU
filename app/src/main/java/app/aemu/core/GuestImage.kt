package app.aemu.core

import org.json.JSONArray
import org.json.JSONObject

/** Настройки конкретного образа, которые пользователь может менять. */
data class VmSettings(
    val width: Int = 540,
    val height: Int = 960,
    val density: Int = 240,
    /** аппаратная отрисовка через GPU телефона (иначе программный растеризатор прошивки) */
    val gpu: Boolean = true,
    /** hwui в гостевых приложениях (4.x) */
    val hwui: Boolean = true,
    /** JIT Dalvik; без него стабильнее, но медленнее */
    val jit: Boolean = true,
    /** ART (5.0+): compile apps fully to machine code at install/first boot; off = interpret-only, fast first boot */
    val fullDexopt: Boolean = false,
    /** частота развёртки гостя, Гц */
    val fbHz: Int = 60,
    /** касаний в секунду, которые шлём гостю */
    val touchHz: Int = 60,
    /** HTTP/HTTPS-прокси с современным TLS для старых браузеров */
    val netProxy: Boolean = true,
    val lowRam: Boolean = false,
    val showFrame: Boolean = false,
    val showNavBar: Boolean = true,
    /** buttons of the bar under the screen, in order: back home recents menu search volup voldown power dpad */
    val navKeys: String = NAV_DEFAULT,
    /** size of the button bar: 0 compact, 1 large */
    val navSize: Int = 0,
    val keepScreenOn: Boolean = true,
    /** frames go to the panel on its vsync (no tearing, no dropped frames); off: shown at once, lower latency, may tear */
    val vsync: Boolean = true,
    /** the guest phone's notification LED drawn above its screen */
    val showLed: Boolean = true,
    val mtMode: Int = 0,
    /** старый движок для 2.x (GL через pbuffer, только GLES 1.x) — запасной вариант */
    val legacyEngine: Boolean = false,
    /** a 32-bit 7.1 firmware on the open engine: kernel 25 with qemu-arm (or aemu32), binder protocol 7 */
    val open32: Boolean = false,
    /** guest RAM budget in MB, 0 = automatic */
    val ramMb: Int = 0,
    /** internal storage the guest reports, MB; 0: derived from the phone's free space */
    val diskMb: Int = 0,
    /** emulate the radio (RIL); off = tablet-like firmware without telephony */
    val radio: Boolean = true,
    /** hardware 32-bit (AArch32 EL0) bypass without translator on supported CPUs */
    val native32: Boolean = false,
    /** IMEI reported by the fake modem; blank = default sample IMEI */
    val imei: String = "",
    /** extra qemu options; KEY=VALUE tokens are passed as environment variables */
    val qemuArgs: String = "",
) {
    fun toJson(): JSONObject = JSONObject()
        .put("width", width).put("height", height).put("density", density)
        .put("gpu", gpu).put("hwui", hwui).put("jit", jit).put("fullDexopt", fullDexopt).put("fbHz", fbHz)
        .put("touchHz", touchHz).put("netProxy", netProxy).put("lowRam", lowRam)
        .put("showFrame", showFrame).put("showNavBar", showNavBar).put("navKeys", navKeys).put("navSize", navSize)
        .put("keepScreenOn", keepScreenOn).put("vsync", vsync).put("showLed", showLed).put("mtMode", mtMode).put("legacyEngine", legacyEngine).put("open32", open32)
        .put("ramMb", ramMb).put("diskMb", diskMb).put("radio", radio).put("native32", native32).put("imei", imei).put("qemuArgs", qemuArgs)

    companion object {
        const val NAV_DEFAULT = "back,home,recents,menu"
        val NAV_ALL = listOf("back", "home", "recents", "menu", "search", "voldown", "volup", "power", "dpad")

        /** Luhn-valid sample IMEI from the standard; the guest has no real modem */
        const val DEFAULT_IMEI = "490154203237518"

        /** Random Luhn-valid IMEI with a test TAC prefix. */
        fun randomImei(): String {
            val d = IntArray(14) { if (it < 2) intArrayOf(3, 5)[it] else (0..9).random() }
            var sum = 0
            for (i in 0 until 14) { var x = d[i]; if (i % 2 == 1) { x *= 2; if (x > 9) x -= 9 }; sum += x }
            return d.joinToString("") + ((10 - sum % 10) % 10)
        }

        fun fromJson(o: JSONObject?): VmSettings {
            if (o == null) return VmSettings()
            val d = VmSettings()
            return VmSettings(
                width = o.optInt("width", d.width),
                height = o.optInt("height", d.height),
                density = o.optInt("density", d.density),
                gpu = o.optBoolean("gpu", d.gpu),
                hwui = o.optBoolean("hwui", d.hwui),
                jit = o.optBoolean("jit", d.jit),
                fullDexopt = o.optBoolean("fullDexopt", d.fullDexopt),
                fbHz = o.optInt("fbHz", d.fbHz),
                touchHz = o.optInt("touchHz", d.touchHz),
                netProxy = o.optBoolean("netProxy", d.netProxy),
                lowRam = o.optBoolean("lowRam", d.lowRam),
                showFrame = o.optBoolean("showFrame", d.showFrame),
                showNavBar = o.optBoolean("showNavBar", d.showNavBar),
                navKeys = o.optString("navKeys", d.navKeys),
                navSize = o.optInt("navSize", d.navSize),
                keepScreenOn = o.optBoolean("keepScreenOn", d.keepScreenOn),
                vsync = o.optBoolean("vsync", d.vsync),
                showLed = o.optBoolean("showLed", d.showLed),
                mtMode = o.optInt("mtMode", d.mtMode),
                legacyEngine = o.optBoolean("legacyEngine", false),
                open32 = o.optBoolean("open32", false),
                ramMb = o.optInt("ramMb", 0),
                diskMb = o.optInt("diskMb", 0),
                radio = o.optBoolean("radio", true),
                native32 = o.optBoolean("native32", false),
                imei = o.optString("imei", ""),
                qemuArgs = o.optString("qemuArgs", ""),
            )
        }
    }
}

/** Служба гостя, которую мы запускаем вместо init. */
data class GuestService(
    val name: String,
    val argv: List<String>,
    /** сокеты init: имя → права (например "zygote" → "0666") */
    val sockets: Map<String, String> = emptyMap(),
    val uid: Int = 0,
    val gid: Int = 0,
    /** ждать появления этого сокета перед следующей службой */
    val waitSocket: String? = null,
    val delayMs: Long = 0,
    val restart: Boolean = false,
    /** вендорский демон: если упадёт — не страшно */
    val optional: Boolean = false,
    /** not started at boot: the guest asks for it with ctl.start (a service outside init's started classes) */
    val onDemand: Boolean = false,
) {
    fun toJson(): JSONObject = JSONObject()
        .put("optional", optional).put("onDemand", onDemand)
        .put("name", name)
        .put("argv", JSONArray(argv))
        .put("sockets", JSONObject(sockets as Map<*, *>))
        .put("uid", uid).put("gid", gid)
        .put("waitSocket", waitSocket ?: JSONObject.NULL)
        .put("delayMs", delayMs).put("restart", restart)

    companion object {
        fun fromJson(o: JSONObject): GuestService {
            val argv = o.getJSONArray("argv").let { a -> List(a.length()) { a.getString(it) } }
            val socks = o.optJSONObject("sockets")?.let { s -> s.keys().asSequence().associateWith { s.getString(it) } } ?: emptyMap()
            return GuestService(
                name = o.getString("name"),
                argv = argv,
                sockets = socks,
                uid = o.optInt("uid", 0),
                gid = o.optInt("gid", 0),
                waitSocket = if (o.isNull("waitSocket")) null else o.optString("waitSocket"),
                delayMs = o.optLong("delayMs", 0),
                restart = o.optBoolean("restart", false),
                optional = o.optBoolean("optional", false),
                onDemand = o.optBoolean("onDemand", false),
            )
        }
    }
}

/**
 * Импортированный образ прошивки. Всё, что нужно для запуска, выводится из самой прошивки
 * при импорте (build.prop, init*.rc, состав /system) — никаких захардкоженных профилей устройств.
 */
data class GuestImage(
    val id: String,
    val name: String,
    val release: String,
    val api: Int,
    val brand: String,
    val model: String,
    val skin: String,
    val engine: Engine,
    val abi: String = "armeabi-v7a",
    val bootclasspath: String,
    val exports: Map<String, String>,
    val dirs: List<String>,
    val services: List<GuestService>,
    val sdcardPath: String,
    /** тома из storage_list.xml, которыми управляет vold (не эмулируемые): о них должна знать заглушка vold */
    val volumes: List<String> = emptyList(),
    val settings: VmSettings,
    val createdAt: Long,
    val sizeBytes: Long = 0,
    val sourceName: String = "",
    val runtime: String = "dalvik",
    val warnings: List<String> = emptyList(),
    val lastBootMs: Long = 0,
    val bootCount: Int = 0,
    /** версия анализатора, которым построен профиль; устаревший профиль пересчитывается перед запуском */
    val profileVersion: Int = 0,
    /** container: id of the image whose /system this one shares (its own /data, card and settings) */
    val baseId: String = "",
    /** The actual matching fingerprint read during import; null for older instances. */
    val fingerprint: String? = null,
) {
    val displayVersion: String get() = "Android $release (API $api)"

    /** Движок с учётом выбора пользователя. */
    fun effective(): GuestImage = copy(engine = when {
        abi == "arm64-v8a" -> Engine.N64
        // every 32-bit firmware runs on the open engine (open32: qemu + kernel 23), whatever an older import wrote in
        // its settings: the closed engines (KK, GB) are not chosen any more
        else -> Engine.N64
    })

    fun toJson(): JSONObject = JSONObject()
        .put("id", id).put("name", name).put("release", release).put("api", api)
        .put("brand", brand).put("model", model).put("skin", skin)
        .put("engine", engine.id).put("abi", abi)
        .put("bootclasspath", bootclasspath)
        .put("exports", JSONObject(exports as Map<*, *>))
        .put("dirs", JSONArray(dirs))
        .put("services", JSONArray(services.map { it.toJson() }))
        .put("sdcardPath", sdcardPath)
        .put("volumes", JSONArray(volumes))
        .put("settings", settings.toJson())
        .put("createdAt", createdAt).put("sizeBytes", sizeBytes)
        .put("sourceName", sourceName).put("runtime", runtime)
        .put("warnings", JSONArray(warnings))
        .put("lastBootMs", lastBootMs).put("bootCount", bootCount)
        .put("profileVersion", profileVersion)
        .put("baseId", baseId)
        .apply { if (fingerprint != null) put("fingerprint", fingerprint) }

    companion object {
        fun fromJson(o: JSONObject): GuestImage {
            val ex = o.optJSONObject("exports")
            val exports = ex?.keys()?.asSequence()?.associateWith { ex.getString(it) } ?: emptyMap()
            val dirs = o.optJSONArray("dirs")?.let { a -> List(a.length()) { a.getString(it) } } ?: emptyList()
            val svcs = o.optJSONArray("services")?.let { a -> List(a.length()) { GuestService.fromJson(a.getJSONObject(it)) } } ?: emptyList()
            val warn = o.optJSONArray("warnings")?.let { a -> List(a.length()) { a.getString(it) } } ?: emptyList()
            return GuestImage(
                id = o.getString("id"),
                name = o.optString("name", "Android"),
                release = o.optString("release", "?"),
                api = o.optInt("api", 19),
                brand = o.optString("brand", ""),
                model = o.optString("model", ""),
                skin = o.optString("skin", "AOSP"),
                engine = Engine.byId(o.optString("engine")),
                abi = o.optString("abi", "armeabi-v7a"),
                bootclasspath = o.optString("bootclasspath", ""),
                exports = exports,
                dirs = dirs,
                services = svcs,
                sdcardPath = o.optString("sdcardPath", "/mnt/sdcard"),
                volumes = o.optJSONArray("volumes")?.let { a -> List(a.length()) { a.getString(it) } } ?: emptyList(),
                settings = VmSettings.fromJson(o.optJSONObject("settings")),
                createdAt = o.optLong("createdAt", 0),
                sizeBytes = o.optLong("sizeBytes", 0),
                sourceName = o.optString("sourceName", ""),
                runtime = o.optString("runtime", "dalvik"),
                warnings = warn,
                lastBootMs = o.optLong("lastBootMs", 0),
                bootCount = o.optInt("bootCount", 0),
                profileVersion = o.optInt("profileVersion", 0),
                baseId = o.optString("baseId", ""),
                fingerprint = if (o.has("fingerprint") && !o.isNull("fingerprint")) o.optString("fingerprint", "") else null,
            )
        }
    }
}
