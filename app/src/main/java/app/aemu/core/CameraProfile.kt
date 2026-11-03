package app.aemu.core

import android.content.Context
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.graphics.ImageFormat
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import kotlin.math.atan
import kotlin.math.roundToInt

/**
 * The camera service between the phone's cameras and the guest's camera HAL:
 *   1. probe — what each camera of the phone can do (camera2 CameraCharacteristics);
 *   2. adapt — that turned into the guest's own format: the flattened android.hardware.Camera parameters
 *      ("key=value;…", CameraParameters.cpp) with only the keys the guest's API level knows;
 *   3. user — the edits from the ⋮ menu (files/images/<id>/camera.json): any key set, added or removed, and per camera
 *      which phone camera feeds it, facing, orientation, mirror; cameras can be added or removed.
 * The HAL asks for "info" (count, facing, orientation) and "params" (the string above) and sends back what the
 * guest application sets; CameraFeed applies that to the phone's camera.
 * Now served to the camera HAL of 4.0–4.4 (camera device API 1.0, API 14–19); other versions keep their HAL's own list.
 */
object CameraProfile {
    /** guest API levels the service builds parameters for */
    val API_RANGE = 14..19

    class Cam(
        var host: String,               // phone camera id
        var facing: Int,                // 0 back, 1 front (CameraInfo.facing)
        var orientation: Int,           // CameraInfo.orientation
        var mirror: Boolean = false,    // flip the frame left-right
        val set: LinkedHashMap<String, String> = LinkedHashMap(),
        val remove: MutableSet<String> = linkedSetOf(),
    )

    class Config(val cams: MutableList<Cam>, var auto: Boolean)

    fun file(paths: VmPaths) = File(paths.dir, "camera.json")

    /** the phone's cameras: back ones first, then front, then the rest (external) */
    fun phoneCameras(ctx: Context): List<Pair<String, CameraCharacteristics>> {
        val cm = ctx.getSystemService(CameraManager::class.java) ?: return emptyList()
        return runCatching { cm.cameraIdList.map { it to cm.getCameraCharacteristics(it) } }.getOrDefault(emptyList())
            .sortedBy { (_, c) -> when (c.get(CameraCharacteristics.LENS_FACING)) { CameraCharacteristics.LENS_FACING_BACK -> 0; CameraCharacteristics.LENS_FACING_FRONT -> 1; else -> 2 } }
    }

    /** the default: the phone's first back and first front camera, as a phone of that time had */
    fun autoCams(ctx: Context): MutableList<Cam> {
        val all = phoneCameras(ctx)
        val pick = listOfNotNull(
            all.firstOrNull { it.second.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK },
            all.firstOrNull { it.second.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_FRONT })
            .ifEmpty { all.take(1) }
        return pick.map { (id, c) ->
            val front = c.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_FRONT
            Cam(id, if (front) 1 else 0, c.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: if (front) 270 else 90)
        }.toMutableList()
    }

    fun load(ctx: Context, paths: VmPaths): Config {
        val f = file(paths)
        val o = runCatching { JSONObject(f.readText()) }.getOrNull() ?: return Config(autoCams(ctx), true)
        val arr = o.optJSONArray("cameras") ?: JSONArray()
        val cams = (0 until arr.length()).map { i ->
            val c = arr.getJSONObject(i)
            val set = LinkedHashMap<String, String>()
            c.optJSONObject("set")?.let { s -> s.keys().forEach { k -> set[k] = s.getString(k) } }
            val rm = linkedSetOf<String>()
            c.optJSONArray("remove")?.let { r -> for (j in 0 until r.length()) rm += r.getString(j) }
            Cam(c.optString("host", "0"), c.optInt("facing", 0), c.optInt("orientation", 90), c.optBoolean("mirror"), set, rm)
        }.toMutableList()
        return Config(cams.ifEmpty { autoCams(ctx) }, o.optBoolean("auto", false))
    }

    fun save(paths: VmPaths, cfg: Config) {
        val arr = JSONArray()
        cfg.cams.forEach { c ->
            arr.put(JSONObject().put("host", c.host).put("facing", c.facing).put("orientation", c.orientation)
                .put("mirror", c.mirror).put("set", JSONObject(c.set as Map<*, *>)).put("remove", JSONArray(c.remove.toList())))
        }
        file(paths).writeText(JSONObject().put("auto", cfg.auto).put("cameras", arr).toString(1))
    }

    fun reset(paths: VmPaths) { file(paths).delete() }

    /**
     * Since which API level android.hardware.Camera.Parameters has a key (developer.android.com reference,
     * "Added in API level"); a guest older than that never asks for it, a key it does not know is left out.
     */
    private val SINCE = mapOf(
        "zoom-supported" to 8, "max-zoom" to 8, "zoom" to 8, "zoom-ratios" to 8, "smooth-zoom-supported" to 8,
        "exposure-compensation" to 8, "max-exposure-compensation" to 8, "min-exposure-compensation" to 8,
        "exposure-compensation-step" to 8, "focal-length" to 8, "horizontal-view-angle" to 8, "vertical-view-angle" to 8,
        "preview-fps-range" to 9, "preview-fps-range-values" to 9, "focus-distances" to 9,
        "video-size" to 11, "video-size-values" to 11, "preferred-preview-size-for-video" to 11,
        "max-num-focus-areas" to 14, "focus-areas" to 14, "max-num-metering-areas" to 14, "metering-areas" to 14,
        "auto-exposure-lock" to 14, "auto-exposure-lock-supported" to 14, "auto-whitebalance-lock" to 14,
        "auto-whitebalance-lock-supported" to 14, "max-num-detected-faces-hw" to 14, "max-num-detected-faces-sw" to 14,
        "video-snapshot-supported" to 14, "recording-hint" to 14,
        "video-stabilization" to 15, "video-stabilization-supported" to 15,
    )

    /** the parameter values a guest of that API level must not be offered (the mode was added later) */
    private val VALUE_SINCE = mapOf("continuous-video" to 9, "continuous-picture" to 14, "edof" to 9, "torch" to 5)

    private fun focusName(m: Int) = when (m) {
        0 -> "fixed"; 1 -> "auto"; 2 -> "macro"; 3 -> "continuous-video"; 4 -> "continuous-picture"; 5 -> "edof"; else -> null }
    private fun awbName(m: Int) = when (m) {
        1 -> "auto"; 2 -> "incandescent"; 3 -> "fluorescent"; 4 -> "warm-fluorescent"; 5 -> "daylight"
        6 -> "cloudy-daylight"; 7 -> "twilight"; 8 -> "shade"; else -> null }
    private fun effectName(m: Int) = when (m) {
        0 -> "none"; 1 -> "mono"; 2 -> "negative"; 3 -> "solarize"; 4 -> "sepia"; 5 -> "posterize"
        6 -> "whiteboard"; 7 -> "blackboard"; 8 -> "aqua"; else -> null }
    private fun sceneName(m: Int) = when (m) {
        2 -> "action"; 3 -> "portrait"; 4 -> "landscape"; 5 -> "night"; 6 -> "night-portrait"; 7 -> "theatre"
        8 -> "beach"; 9 -> "snow"; 10 -> "sunset"; 11 -> "steadyphoto"; 12 -> "fireworks"; 13 -> "sports"
        14 -> "party"; 15 -> "candlelight"; 16 -> "barcode"; 18 -> "hdr"; else -> null }
    private fun bandName(m: Int) = when (m) { 0 -> "off"; 1 -> "50hz"; 2 -> "60hz"; 3 -> "auto"; else -> null }

    /** the phone camera's abilities as Camera.Parameters of the guest's API level, the order of a real HAL's dump */
    fun auto(c: CameraCharacteristics?, api: Int): LinkedHashMap<String, String> {
        val p = LinkedHashMap<String, String>()
        // the preview comes from the host's 640×480 stream, scaled to the size the application asks for
        val prev = "640x480,320x240,176x144,1280x720,800x480,720x480,352x288"
        p["preview-size"] = "640x480"; p["preview-size-values"] = prev
        p["preview-format"] = "yuv420sp"; p["preview-format-values"] = "yuv420sp,yuv420p"
        val fps = c?.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)?.filter { it.upper <= 30 }
            ?.distinct()?.sortedWith(compareBy({ it.upper }, { it.lower })).orEmpty()
        val rates = fps.map { it.upper }.distinct().ifEmpty { listOf(15, 30) }
        p["preview-frame-rate"] = rates.max().toString(); p["preview-frame-rate-values"] = rates.joinToString(",")
        val ranges = fps.ifEmpty { null }?.joinToString(",") { "(${it.lower * 1000},${it.upper * 1000})" } ?: "(15000,30000)"
        p["preview-fps-range"] = fps.lastOrNull()?.let { "${it.lower * 1000},${it.upper * 1000}" } ?: "15000,30000"
        p["preview-fps-range-values"] = ranges
        // pictures: the phone's own JPEG sizes (a separate still stream), at most 8 MP — the guest's heaps of that time
        val jpeg = c?.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)?.getOutputSizes(ImageFormat.JPEG)
            ?.filter { it.width.toLong() * it.height <= 8_000_000L && it.width <= 4096 && it.height <= 4096 }
            ?.sortedByDescending { it.width.toLong() * it.height }?.distinctBy { "${it.width}x${it.height}" }
            ?.take(12)?.map { "${it.width}x${it.height}" }.orEmpty()
        val pics = (jpeg + "640x480").distinct()
        // default: the largest 4:3 picture, the shape of a phone camera of that time
        p["picture-size"] = pics.firstOrNull { val (w, h) = it.split('x').map(String::toInt); w * 3 == h * 4 } ?: pics.first(); p["picture-size-values"] = pics.joinToString(",")
        p["picture-format"] = "jpeg"; p["picture-format-values"] = "jpeg"; p["jpeg-quality"] = "90"
        p["jpeg-thumbnail-width"] = "320"; p["jpeg-thumbnail-height"] = "240"
        p["jpeg-thumbnail-size-values"] = "0x0,320x240,160x120"; p["jpeg-thumbnail-quality"] = "90"
        val af = c?.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)?.toList()?.mapNotNull { focusName(it) }.orEmpty()
        val minFocus = c?.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f
        val focus = (if (minFocus > 0f) af else listOf("fixed")).let { if ("auto" in it) it + "infinity" else it }.ifEmpty { listOf("fixed") }
        p["focus-mode"] = if ("auto" in focus) "auto" else focus.first(); p["focus-mode-values"] = focus.joinToString(",")
        val f = c?.get(CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS)?.firstOrNull() ?: 3.5f
        val sz = c?.get(CameraCharacteristics.SENSOR_INFO_PHYSICAL_SIZE)
        p["focal-length"] = "%.2f".format(java.util.Locale.US, f)
        p["horizontal-view-angle"] = sz?.let { (Math.toDegrees(2 * atan(it.width / (2.0 * f)))).roundToInt().toString() } ?: "60"
        p["vertical-view-angle"] = sz?.let { (Math.toDegrees(2 * atan(it.height / (2.0 * f)))).roundToInt().toString() } ?: "45"
        p["focus-distances"] = if (minFocus > 0f) "%.2f,%.2f,Infinity".format(java.util.Locale.US, 1f / minFocus, 1f / minFocus * 2) else "Infinity,Infinity,Infinity"
        val ev = c?.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE)
        val step = c?.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_STEP)?.toFloat() ?: 1f
        p["exposure-compensation"] = "0"
        p["max-exposure-compensation"] = (ev?.upper ?: 2).toString(); p["min-exposure-compensation"] = (ev?.lower ?: -2).toString()
        p["exposure-compensation-step"] = "%.6f".format(java.util.Locale.US, step).trimEnd('0').trimEnd('.')
        val awb = c?.get(CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES)?.toList()?.mapNotNull { awbName(it) }.orEmpty().ifEmpty { listOf("auto") }
        p["whitebalance"] = "auto"; p["whitebalance-values"] = awb.joinToString(",")
        val eff = c?.get(CameraCharacteristics.CONTROL_AVAILABLE_EFFECTS)?.toList()?.mapNotNull { effectName(it) }.orEmpty().ifEmpty { listOf("none") }
        p["effect"] = "none"; p["effect-values"] = eff.joinToString(",")
        val band = c?.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_ANTIBANDING_MODES)?.toList()?.mapNotNull { bandName(it) }.orEmpty().ifEmpty { listOf("auto") }
        p["antibanding"] = if ("auto" in band) "auto" else band.first(); p["antibanding-values"] = band.joinToString(",")
        val scene = listOf("auto") + c?.get(CameraCharacteristics.CONTROL_AVAILABLE_SCENE_MODES)?.toList()?.mapNotNull { sceneName(it) }.orEmpty()
        p["scene-mode"] = "auto"; p["scene-mode-values"] = scene.distinct().joinToString(",")
        val flash = c?.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
        p["flash-mode"] = "off"; p["flash-mode-values"] = if (flash) "off,auto,on,torch" else "off"
        // digital zoom: done on the host frame, up to the phone's own limit (at most 4×: the 640×480 frame gets coarse)
        val maxZ = ((c?.get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM) ?: 4f).coerceIn(1f, 4f) * 100).toInt()
        val ratios = (100..maxZ step 20).toList().let { if (it.last() != maxZ) it + maxZ else it }
        p["zoom-supported"] = if (ratios.size > 1) "true" else "false"; p["smooth-zoom-supported"] = "false"
        p["max-zoom"] = (ratios.size - 1).toString(); p["zoom"] = "0"; p["zoom-ratios"] = ratios.joinToString(",")
        p["rotation"] = "0"
        p["video-frame-format"] = "yuv420sp"; p["video-size"] = "640x480"; p["video-size-values"] = "640x480,1280x720"
        p["preferred-preview-size-for-video"] = "640x480"
        val faces = c?.get(CameraCharacteristics.STATISTICS_INFO_MAX_FACE_COUNT) ?: 0
        p["max-num-detected-faces-hw"] = "0"; p["max-num-detected-faces-sw"] = "0"   // no face events are sent yet
        if (faces < 0) p.remove("max-num-detected-faces-hw")
        p["max-num-focus-areas"] = if ((c?.get(CameraCharacteristics.CONTROL_MAX_REGIONS_AF) ?: 0) > 0) "1" else "0"
        p["max-num-metering-areas"] = if ((c?.get(CameraCharacteristics.CONTROL_MAX_REGIONS_AE) ?: 0) > 0) "1" else "0"
        p["video-snapshot-supported"] = "true"
        val stab = c?.get(CameraCharacteristics.CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES)?.any { it != 0 } == true
        p["video-stabilization-supported"] = if (stab) "true" else "false"
        p["auto-exposure-lock-supported"] = if (c?.get(CameraCharacteristics.CONTROL_AE_LOCK_AVAILABLE) != false) "true" else "false"
        p["auto-whitebalance-lock-supported"] = if (c?.get(CameraCharacteristics.CONTROL_AWB_LOCK_AVAILABLE) != false) "true" else "false"
        // what Qualcomm camera applications (MIUI's and others) read as numbers without asking whether the key is there
        p["saturation"] = "5"; p["max-saturation"] = "10"; p["min-saturation"] = "0"; p["saturation-step"] = "1"
        p["contrast"] = "5"; p["max-contrast"] = "10"; p["min-contrast"] = "0"; p["contrast-step"] = "1"
        p["sharpness"] = "2"; p["max-sharpness"] = "6"; p["min-sharpness"] = "0"; p["sharpness-step"] = "1"
        p["brightness"] = "3"; p["max-brightness"] = "6"; p["min-brightness"] = "0"; p["brightness-step"] = "1"
        p["iso"] = "auto"; p["iso-values"] = "auto"; p["zsl"] = "off"; p["zsl-values"] = "off"
        p["face-detection"] = "off"; p["face-detection-values"] = "off"; p["denoise"] = "denoise-off"; p["denoise-values"] = "denoise-off"
        p["redeye-reduction"] = "disable"; p["redeye-reduction-values"] = "disable"
        p["auto-exposure"] = "center-weighted"; p["auto-exposure-values"] = "center-weighted"
        p["touch-af-aec"] = "touch-off"; p["touch-af-aec-values"] = "touch-off"
        p["video-hfr"] = "off"; p["video-hfr-values"] = "off"; p["video-hdr"] = "off"; p["video-hdr-values"] = "off"
        p["num-snaps-per-shutter"] = "1"; p["camera-mode"] = "0"
        p["preview-flip"] = "off"; p["video-flip"] = "off"; p["snapshot-picture-flip"] = "off"
        // the API filter: keys and mode values this guest does not know yet
        p.keys.removeAll { (SINCE[it] ?: 1) > api }
        for (k in p.keys.filter { it.endsWith("-values") || it == "focus-mode" }) {
            p[k] = p[k]!!.split(',').filter { (VALUE_SINCE[it] ?: 1) <= api }.joinToString(",")
        }
        return p
    }

    /** auto parameters with the user's edits applied */
    fun effective(ctx: Context, cam: Cam, api: Int): LinkedHashMap<String, String> {
        val c = phoneCameras(ctx).firstOrNull { it.first == cam.host }?.second
        val p = auto(c, api)
        cam.remove.forEach { p.remove(it) }
        p.putAll(cam.set)
        return p
    }

    fun flatten(p: Map<String, String>) = p.entries.joinToString(";") { "${it.key}=${it.value}" }
    fun unflatten(s: String): LinkedHashMap<String, String> = LinkedHashMap<String, String>().apply {
        s.split(';').forEach { kv -> val i = kv.indexOf('='); if (i > 0) put(kv.substring(0, i), kv.substring(i + 1)) }
    }
}
