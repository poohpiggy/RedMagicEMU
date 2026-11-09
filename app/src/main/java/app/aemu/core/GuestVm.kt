package app.aemu.core

import android.content.Context
import android.os.Process as AProcess
import android.os.SystemClock
import android.view.Surface
import dev.lk.dhd.BinderSlot
import dev.lk.dhd.GlSlot
import dev.lk.dhd.Slot
import dev.lk.m7sense.GlBridge
import java.io.File
import java.io.FileOutputStream
import java.io.OutputStreamWriter
import java.util.concurrent.CopyOnWriteArrayList

/** Daemons system_server cannot live without: restarted even when init.rc marks them oneshot
 * (MIUI installd dies in the LBE security hook on start). */
private val ALWAYS_RESTART = setOf("mediaserver", "installd", "netd", "keystore")

/** Native arm64 engine: vendor daemons that only poll hardware the phone does not have (modem, DSP, TrustZone). */
private val N64_SKIP = setOf("audiod", "ATFWD-daemon", "imsqmidaemon", "loc_launcher", "cnd", "dpmd", "cnss-daemon",
    "tftp_server", "qcom-system-daemon", "hvdcp_opti", "pm-service", "mtd", "mlipayd", "vsimd", "fdpp", "perfd", "ipacm",
    "qti", "netmgrd", "port-bridge", "ims_rtp_daemon", "imsdatadaemon", "wcnss_service", "msm_irqbalance", "energy-awareness", "sscrpcd", "adsprpcd", "cdsprpcd", "audioadsprpcd")

/** True confirms only an inert adapter was prepared, not isolation or service availability. */
internal fun prepareVibrationRuntime(recovery: Boolean, prepare: () -> Boolean, log: (String) -> Unit): Boolean {
    if (recovery) return false
    return try {
        prepare()
    } catch (interrupted: InterruptedException) {
        Thread.currentThread().interrupt()
        throw interrupted
    } catch (failure: Exception) {
        log("vibration: adapter preparation failed (${failure.javaClass.simpleName})")
        false
    }
}

/**
 * Одна запущенная виртуальная машина. Живёт в процессе :vm (одна на процесс: GL-мост нельзя
 * поднять дважды), заменяет собой init: готовит дерево, поднимает службы хоста (свойства, ввод,
 * звук, заглушки радио и накопителя), binderd, GL-мост и службы гостя в порядке загрузки Android.
 */
class GuestVm(val ctx: Context, image: GuestImage) {
    // Offline development only. The limited guard does not prevent access to
    // devices exposed by a mount inside the Guest tree. Never enable in builds.
    private val experimentalVibrationEnabled = false
    // Controlled testing only: Guest caller permission authenticity is not established.
    private val directVibrationEnabled = true
    @Volatile private var directVibrationBlockQti = false
    private val directVibrationLock = Any()
    private var directVibrationBridge: HostVibrationBinderBridge? = null
    @Volatile private var experimentalVibrationPrepared = false
    private val vibrationLock = Any()
    private var vibrationChannel: HostVibrationChannel? = null

    // Select the same runtime engine for every entry point without modifying stored metadata.
    val img = image.effective()

    companion object {
        /** A 32-bit firmware (open32) draws through our open GPU bridge (libGLES_aemu) rather than the 4.4 engine's
         *  closed one: 2.x-5.x by default; 6.0+ behind the test file "glopen" until checked by screenshot (PLAN 3.1.1);
         *  "glclosed" puts the closed one back for a comparison. Shared by TreeFixer (libraries) and the boot (GlHost). */
        fun open32Bridge(engine: Engine, img: GuestImage, dir: File): Boolean =
            engine == Engine.N64 && img.abi != "arm64-v8a" && !File(dir, "glclosed").isFile &&
                (img.api < 23 || File(dir, "glopen").isFile)
        /** debug.aemu.gl of an open32 firmware (guest.c policy): the text of "glopen" if there is one; 4.4 defaults to
         *  3 - the compositor and everything else on the open bridge, which then serves the display itself (guest.c
         *  own_egl) and no closed GL server runs (desktop, tester, 16 4.x images by screenshot 03.11); the rest 2 (the
         *  compositor on the closed bridge). 4.0-4.3 compose in GLES 1 from window buffers in memory: with 3 a GL
         *  program's frame is read back and uploaded again (tester gl1 119 -> 79 fps on 4.3) until the 32-bit gralloc
         *  keeps buffers on the host (PLAN 3.1); 5.x not checked yet */
        fun open32Mode(img: GuestImage, dir: File): Int =
            runCatching { File(dir, "glopen").readText().trim().toInt() }.getOrDefault(if (img.api == 19) 3 else 2)
        /** the compositor of an open32 firmware draws through the open bridge, which reads its framebuffer target
         *  back into fb0 as RGBA_8888 - the format the kernel reports (run64.c fb_ioctl); otherwise the closed GL
         *  server writes RGB565 frames there */
        fun open32Compositor(engine: Engine, img: GuestImage, dir: File): Boolean =
            open32Bridge(engine, img, dir) && (img.api >= 23 || open32Mode(img, dir) >= 3)
    }
    enum class State { STOPPED, PREPARING, BOOTING, RUNNING, FAILED, STOPPING }

    val paths = VmPaths(ctx, img.id)
    val kernel: Kernel64 get() = Kernel64.forApi(img.api)
    val settings get() = img.settings
    val engine get() = img.engine

    @Volatile var state = State.STOPPED
        private set
    private val admissionState = AdmissionState()
    @Volatile var bootAt = 0L
        private set
    @Volatile var bootDoneAt = 0L
    // the boot animation has gone: the lock screen or the home screen is up (7.0+ reports boot_completed only after the user unlocks)
    @Volatile var screenUpAt = 0L
    /** the system reached the home screen at least once in this run: later crashes restart it instead of failing */
    @Volatile private var everBooted = false
    /** Times of the zygote crashes: a system that keeps dying is stopped instead of being restarted forever. */
    private val zygoteCrashes = ArrayDeque<Long>()

    /**
     * The services to start, read from the firmware's init.rc at every boot: the list stored at import was made by the
     * rules of the version that imported it, and fixes to those rules must reach firmware imported earlier.
     */
    private fun servicePlan(): List<GuestService> = runCatching {
        val root = paths.root
        val props = HashMap<String, String>()
        for (n in listOf("default.prop", "system/build.prop")) runCatching {
            File(root, n).forEachLine { l -> val i = l.indexOf('='); if (i > 0 && !l.startsWith("#")) props[l.substring(0, i).trim()] = l.substring(i + 1).trim() }
        }
        val rcFiles = (root.listFiles()?.filter { it.isFile && it.name.endsWith(".rc") }?.sortedBy { if (it.name == "init.rc") 0 else 1 } ?: emptyList()) +
            // M3/M5 keep it in /etc
            listOfNotNull(File(root, "etc/init.rc").takeIf { it.isFile && !File(root, "init.rc").isFile }) +
            InitPlan.hwRcFiles(root, props) +
            listOf("system/etc/init", "vendor/etc/init").flatMap { d -> File(root, d).listFiles()?.filter { it.name.endsWith(".rc") }?.sortedBy { it.name } ?: emptyList() } +
            // 11+: a module's services are declared inside the module (the software codec service)
            (File(root, "apex").listFiles()?.sortedBy { it.name }?.flatMap { m -> File(m, "etc").listFiles()?.filter { it.isFile && it.name.endsWith(".rc") }?.sortedBy { it.name } ?: emptyList() } ?: emptyList())
        InitPlan.plan(InitPlan.parse(rcFiles, props), img.api, root).takeIf { p -> p.any { it.name == "zygote" } }
    }.getOrNull() ?: img.services.ifEmpty { InitPlan.fallback(img, paths.root) }
    private var zygoteRestarts = 0
        private set
    @Volatile var failure: String? = null
        private set
    @Volatile var surface: Surface? = null
        set(v) { field = v; if (engine == Engine.KK) GlBridge.surface(v) else if (engine == Engine.N64 && kernel.gpuBridge64) { GlHost.vsync(settings.vsync); GlHost.surface(v) } }

    private val lines = CopyOnWriteArrayList<String>()
    private val listeners = CopyOnWriteArrayList<(String) -> Unit>()
    private val stateListeners = CopyOnWriteArrayList<(State) -> Unit>()
    private val stateLock = Any()
    private val logFile = File(paths.bin, "aemu.log")

    fun log(s: String) {
        val line = "${stamp()} $s"
        lines.add(line)
        while (lines.size > 2000) lines.removeAt(0)
        admissionState.ifAdmitted {
            runCatching {
                if (engine == Engine.N64 && kernel.syncLog) FileOutputStream(logFile, true).use { it.write((line + "\n").toByteArray()); it.fd.sync() }
                else logFile.appendText(line + "\n")
            }
        }
        listeners.forEach { runCatching { it(line) } }
    }
    fun lines(): List<String> = lines.toList()
    fun onLog(l: (String) -> Unit): () -> Unit {
        listeners.add(l)
        return { listeners.remove(l); Unit }
    }
    fun onState(l: (State) -> Unit): () -> Unit {
        synchronized(stateLock) { stateListeners.add(l) }
        return { synchronized(stateLock) { stateListeners.remove(l); Unit } }
    }
    fun observeState(l: (State) -> Unit): () -> Unit {
        synchronized(stateLock) {
            stateListeners.add(l)
            runCatching { l(state) }
        }
        return { synchronized(stateLock) { stateListeners.remove(l); Unit } }
    }
    private fun setState(s: State) {
        fun publish() = synchronized(stateLock) { state = s; stateListeners.forEach { runCatching { it(s) } } }
        if (s == State.STOPPING || s == State.STOPPED) publish() else lifecycle.ifRunning { publish() }
    }

    val props = PropService(paths, ::log)
    /** ADB over the LAN, switched on from the VM menu */
    val adb = AdbServer(this)
    val input = InputService(paths, ::log)
    // 2.x пишет в /dev/eac через AudioHardwareGeneric на 44,1 кГц, HAL 4.x движка — на 48 кГц
    // Do not inspect the firmware tree during VM construction, before admission.
    private val audioDelegate = lazy {
        AudioOut(paths, ::log, if (img.api < 14) 44100 else AudioOut.RATE,
            if (TreeFixer.isMtkAudio(paths.root)) "dev/aemu_pcm" else "dev/eac")
    }
    val audio by audioDelegate
    val ril = RilStub(paths, ::log, img.settings.imei.ifBlank { VmSettings.DEFAULT_IMEI }, img.api)
    @Volatile private var activeRadioBackend = TargetRadioLaunch.Backend.DISABLED
    @Volatile private var radioEvents: RadioHostChannel? = null

    internal fun radioEventGeneration(): Long? = lifecycle.currentGeneration()

    internal fun sendVirtualSms(from: String, body: String, generation: Long): RadioSubmitResult =
        lifecycle.runForGeneration(generation) {
            TargetRadioLaunch.routeEvent(activeRadioBackend, { radioEvents?.sms(from, body) ?: RadioSubmitResult.UNAVAILABLE },
                { ril.sms(from, body) })
        }

    internal fun ringVirtualCall(from: String, generation: Long): RadioSubmitResult =
        lifecycle.runForGeneration(generation) {
            TargetRadioLaunch.routeEvent(activeRadioBackend, { radioEvents?.call(from) ?: RadioSubmitResult.UNAVAILABLE },
                { ril.ring(from.trim()) })
        }
    // 2.x-3.x: wifi.c / dhcp_utils.c era; 4.0+ talks to p2p_supplicant and netd differently (not emulated yet)
    val wifi = if (img.api in 5..13) WifiStub(paths, img.api, props, "8.8.8.8" to "1.1.1.1", ::log) else null
    val vold = VoldStub(paths, img.sdcardPath, ::log, others = img.volumes, api = img.api, setProp = { k, v -> props.put(k, v) })
    var onFrame: (() -> Unit)? = null
    val frames = FrameBell(paths, ::log) { onFrame?.invoke() }
    private val events = EventsSink(paths, ::log)
    private val logd = LogdSink(paths, ::log)
    /** notification / battery LED of the guest (lights HAL → /dev/aemu_lights) */
    val lights = Lights(paths.root, ::log)
    /** the phone's accelerometer and vibration motor for the guest */
    // Only the admitted target firmware is supported; Guest API metadata is not a trust boundary.
    val sensors = Sensors(ctx, paths.root, legacyVibration = false, log = ::log)
    /** the phone's microphone for the guest (audio HAL input → /dev/aemu_mic) */
    val mic = AudioIn(ctx, paths.root, ::log)
    /** the phone's cameras for the guest (camera HAL → /dev/socket/aemu_cam) */
    val camera = CameraFeed(ctx, paths, img.api, ::log)
    /** the phone's video encoder for the guest's OMX encoder (native/omxvenc → /dev/socket/aemu_venc) */
    val venc = VideoEncoderFeed(paths, ::log)
    /** hardware overlays of 2.x (overlay HAL → /dev/socket/aemu_ovl), drawn over the screen */
    val overlays = OverlayFeed(paths, ::log)
    @Volatile private var lmk: GuestLmk? = null
    val net = NetProxy(ctx, paths, img.api, img.abi != "arm64-v8a", ::log)
    private val dns = DnsProxy(paths, img.api, img.engine == Engine.N64 && Kernel64.forApi(img.api).dnsLayoutProbe, ::log)
    val runner by lazy { GuestRunner(paths, img).also { it.processStarter = ::startExternalProcess } }
    private val guestLauncher by lazy { PidfdGuestLauncher(File(ctx.applicationInfo.nativeLibraryDir), ::log, ::pidOf) }

    private val extraStubs = ArrayList<VoldStub>()
    private val procs = LinkedHashMap<String, Process>()
    private val externalProcs = java.util.concurrent.ConcurrentHashMap.newKeySet<Process>()
    private val lazyStarts = HashMap<String, Int>()
    @Volatile private var stopping = false
    @Volatile var bootStartedElapsed: Long = 0L
        private set
    private val lifecycle = VmLifecycle()
    internal fun withAdmittedFramebuffer(action: () -> Unit) = admissionState.ifAdmitted {
        if (state == State.PREPARING || state == State.BOOTING || state == State.RUNNING) action()
    }

    // ------------------------------------------------------------------ загрузка

    fun bootAsync() {
        if (!lifecycle.begin {
                stopping = false
                bootStartedElapsed = SystemClock.elapsedRealtime()
                failure = null
                powerHandled.set(false)
                admissionState.reset()
                setState(State.PREPARING)
            }) return
        try {
            Thread({
                try {
                    requireSystemBoot(recoveryMode)
                    admissionState.verify {
                        InstanceAdmission.requireVerified(paths.dir, img.fingerprint)
                        GuestWritePaths.requireSafe(paths.root, img.dirs)
                    }
                    if (!stopping) {
                        audioDelegate.value // Probe before fixup, as in the original boot sequence.
                        if (!stopping) { if (recoveryMode) doRecovery() else doBoot() }
                    }
                } catch (t: Throwable) {
                    activeRadioBackend = TargetRadioLaunch.Backend.DISABLED
                    closeDirectVibration()?.let { t.addSuppressed(it) }
                    closeExperimentalVibration()?.let { t.addSuppressed(it) }
                    closeRadioChannelForStop(radioEvents) { radioEvents = null }?.let { cleanup ->
                        log("radio: event channel cleanup failed: ${cleanup.javaClass.simpleName}")
                    }
                    if (!stopping) {
                        failure = t.message ?: t.toString()
                        log("✖ boot aborted: $failure")
                        setState(State.FAILED)
                    }
                } finally {
                    lifecycle.endBoot()
                }
            }, "vm-boot").start()
        } catch (t: Throwable) {
            lifecycle.endBoot()
            failure = t.message ?: t.toString()
            setState(State.FAILED)
            throw t
        }
    }

    /** Boot the firmware's recovery (RecoveryImage) instead of Android. */
    @Volatile var recoveryMode = false
    /** recovery framebuffer format for the screen view: 0 RGB565, 1 RGBA/RGBX_8888, 2 BGRA_8888 */
    @Volatile var recoveryFormat = 0
    /** a 32-bit firmware on the arm64 kernel (open32) with the 4.4 engine's GL server: fb0 holds RGB565 frames, as on that engine */
    // 6.0+ under the open bridge: the compositor's framebuffer target (GRALLOC_USAGE_HW_FB) is read back by the guest
    // bridge into fb0 as RGBA_8888, the format the kernel reports (run64.c fb_ioctl) - not the GL server's RGB565
    val fb565: Boolean get() = engine == Engine.N64 && img.abi != "arm64-v8a" && settings.gpu && !open32Compositor(engine, img, paths.dir)
    /** 0.9-1.x: SurfaceFlinger draws RGB565 itself (whatever bpp fbdev reports) and flips between two such pages */
    val fbLegacy565: Boolean get() = engine == Engine.N64 && img.api < 3

    private fun doRecovery() {
        paths.bin.mkdirs()
        runCatching { logFile.writeText("") }
        log("image \"${img.name}\": recovery mode")
        if (!RecoveryImage.installed(paths)) error("this firmware has no recovery installed")
        val is64 = RecoveryImage.isElf64(paths)
        val qemu = paths.nativeBin(if (is64) kernel.runner else Engine.KK.qemu)
        if (!is64 && !(img.settings.native32 && CpuInfo.hasAArch32) && !qemu.canExecute()) error("translator ${Engine.KK.qemu} is not executable")
        killLeftovers()
        HostNative.limitStackSafe()
        TreeFixer(ctx, paths, img, ::log).fixup()   // framebuffer, input node
        val sd = Sdcard.setup(ctx, paths, img, ::log)
        RecoveryImage.prepare(paths, sd, settings)
        input.rateHz = settings.touchHz
        input.mtMode = if (img.api < 5) 4 else settings.mtMode   // ABS_MT_* came in 2.0: 1.x KeyInputQueue reads ABS_X/Y only
        input.serve()
        // recovery's minui opens /dev/input/event* through openat():
        // a FIFO there gets the raw input_event stream straight from InputService
        runCatching {
            val ev = File(RecoveryImage.dir(paths), "dev/input/event0")
            ev.delete()
            android.system.Os.mkfifo(ev.path, "666".toInt(8))
            val fd = android.system.Os.open(ev.path, android.system.OsConstants.O_RDWR, 0)
            input.sink = java.io.FileOutputStream(fd)
            for (alt in listOf("event1", "event2", "event3")) {
                val f = File(RecoveryImage.dir(paths), "dev/input/$alt")
                f.delete()
                runCatching { android.system.Os.symlink(ev.path, f.path) }
            }
        }.onFailure { log("recovery: no input channel: ${it.message}") }
        val (w, h) = runCatching { File(paths.root, "dhd.fbgeom").readText().trim().split(Regex("\\s+")).map { it.toInt() } }
            .getOrNull()?.takeIf { it.size >= 2 }?.let { it[0] to it[1] } ?: (settings.width to settings.height)
        val r = RecoveryImage.dir(paths)
        runCatching { File(r, "tmp/recovery.log").delete() }
        // debugging: run/recovery.strace traces the guest's system calls into run/recovery.trace
        val trace = if (File(paths.bin, "recovery.strace").exists()) listOf("-strace", "-D", File(paths.bin, "recovery.trace").absolutePath) else emptyList()
        val cmd = when {
            img.settings.native32 && CpuInfo.hasAArch32 && !is64 -> listOf(File(r, "sbin/recovery").absolutePath)
            is64 -> listOf(qemu.absolutePath, "/sbin/recovery", File(r, "sbin/recovery").absolutePath)
            else -> listOf(qemu.absolutePath, "-L", r.absolutePath, "-0", "/sbin/recovery") + trace + File(r, "sbin/recovery").absolutePath
        }
        val pb = ProcessBuilder(cmd)
            .directory(r).redirectErrorStream(true).redirectOutput(File(paths.bin, "recovery.out"))
        pb.environment().clear()
        pb.environment().putAll(mapOf(
            "PATH" to "/sbin:/system/bin", "LD_LIBRARY_PATH" to ".:/sbin:/system/lib64:/system/lib", "ANDROID_ROOT" to "/system", "ANDROID_DATA" to "/data",
            "EXTERNAL_STORAGE" to "/sdcard", "TZ" to "UTC",
            "LD_PRELOAD" to "/sbin/librecshim.so:/sbin/libaemushim.so", "AEMU_MOUNTS" to "/sdcard:/data:/system:/cache:/emmc:/external_sd:/usb-otg",
            "DHD_FB_W" to "$w", "DHD_FB_H" to "$h", "DHD_IN_W" to "$w", "DHD_IN_H" to "$h",
            "DHD_INPUT" to paths.inputSock.absolutePath,
        ))
        bootAt = System.currentTimeMillis()
        val p = pb.start()
        synchronized(procs) { procs["recovery"] = p }
        setState(State.RUNNING)
        log("recovery started (${w}x$h)")
        // pixel format the recovery draws in, from its own log ("Pixel format: BGRA_8888", TWRP)
        Thread {
            val rlog = File(r, "tmp/recovery.log")
            repeat(40) {
                if (!p.isAlive || stopping) return@Thread
                val t = runCatching { rlog.readText() }.getOrDefault("")
                val fmt = Regex("Pixel format: (BGRA_8888|RGBX_8888|RGBA_8888|RGB_565)").findAll(t).lastOrNull()?.groupValues?.get(1)
                if (fmt != null) {
                    recoveryFormat = when (fmt) { "BGRA_8888" -> 2; "RGB_565" -> 0; else -> 1 }
                    log("recovery draws $fmt")
                    return@Thread
                }
                Thread.sleep(250)
            }
        }.start()
        // reboot requests of the recovery (librecshim → /dev/aemu_power)
        val power = File(r, "dev/aemu_power")
        Thread {
            while (p.isAlive && !stopping) {
                if (power.length() > 0) {
                    val v = runCatching { power.readText().trim() }.getOrDefault("reboot"); power.writeText("")
                    powerRequest(v); break
                }
                Thread.sleep(500)
            }
        }.start()
        Thread {
            val code = runCatching { p.waitFor() }.getOrDefault(-1)
            if (stopping || powerHandled.get()) return@Thread
            log("recovery exited (code $code), booting the system")
            onPower?.invoke(true, "")
        }.start()
    }

    private fun doBoot() {
        paths.bin.mkdirs()
        runCatching { logFile.writeText("") }
        runCatching { PowerRequestHistory(paths.bin).read() }
            .onSuccess { request -> if (request != null) log("previous guest power request: $request") }
            .onFailure { error -> log("cannot read previous guest power request: ${error.message}") }
        log("image \"${img.name}\": ${img.displayVersion}, ${img.skin}, engine ${engine.title}")
        if (!File(paths.root, "system/framework").isDirectory) error("firmware tree missing")
        val qemu = paths.nativeBin(if (engine == Engine.N64) kernel.runner else engine.qemu)
        if (!qemu.canExecute()) error("translator ${qemu.name} is not executable")
        if (engine == Engine.N64) log("· kernel ${kernel.tag} (${kernel.runner})")
        // the guest's SwiftShader (legacy, reads SwiftShader.ini from the process's directory, this one) finds one core
        // on Android and draws every frame in a single thread: MIUI 8 at 1080×1920 flipped pages at 19 fps, 59 with 8.
        // Every N64 kernel ships that build (n64-gl), and the guest's services and apps all run in this directory
        // (run/SwiftShader.off: the old single thread, to compare)
        if (engine == Engine.N64) runCatching {
            if (File(paths.bin, "SwiftShader.off").exists()) File(paths.bin, "SwiftShader.ini").delete()
            else File(paths.bin, "SwiftShader.ini").writeText("[Processor]\nThreadCount=${Runtime.getRuntime().availableProcessors().coerceIn(1, 8)}\n")
        }
        killLeftovers()
        HostNative.limitStackSafe()
        LogCap.trimAtBoot(paths)
        LogCap.watch(paths) { stopping || state == State.STOPPED || state == State.FAILED }

        // 1. дерево
        val fixer = TreeFixer(ctx, paths, img, ::log)
        fixer.fixup()
        val vibrationAdapterReady = prepareVibrationRuntime(recoveryMode, {
            TargetVibrationArtifacts.prepare(paths.root, img.api, img.model, img.abi, engine, true) { asset ->
                EngineAssets.open(ctx, asset).use { it.readBytes() }
            }
        }, ::log)
        experimentalVibrationPrepared = prepareExperimentalVibration(experimentalVibrationEnabled, vibrationAdapterReady) { enabled ->
            TargetVibrationDeclaration.prepare(paths.root, img.api, img.model, img.abi, engine, enabled)
        }
        // политика звука уже подменялась раньше — обновить её файлы (обёртка/AOSP) до текущей версии
        if (File(paths.root, "system/.aemu-parked/system#lib#hw#audio_policy.default.so").isFile) swapAudioPolicy()
        GuestLog.clear(paths.root)
        val sd = Sdcard.setup(ctx, paths, img, ::log)

        // 2. свойства
        val s = settings
        val radioBackend = TargetRadioLaunch.select(img.api, img.model, img.abi, engine, s.radio)
        val eventChannel = lifecycle.create {
            val channel = if (radioBackend == TargetRadioLaunch.Backend.AIDL) RadioHostChannel.prepare() else null
            radioEvents = channel
            activeRadioBackend = radioBackend
            channel
        }
        val aidlRadio = if (eventChannel != null) TargetRadioLaunch.service(paths.root, eventChannel.socketName,
            AProcess.myPid(), AProcess.myUid(), eventChannel.epoch) else null
        val overrides = LinkedHashMap<String, String>()
        // 2.3: ro.kernel.qemu=1 включает в фреймворке режим эмулятора — ставим его только mediaserver
        overrides["ro.kernel.qemu"] = if (engine == Engine.GB) "0" else "1"
        overrides["ro.kernel.qemu.gles"] = if (s.gpu && (s.hwui || engine == Engine.GB)) "1" else "0"
        overrides["qemu.gles"] = if (s.gpu) "1" else "0"
        if (engine == Engine.N64) {             // SwiftShader (GLES 2/3 on the CPU) where the engine has it for this release
            // (8.0+ libEGL picks the driver by name: 1 is the emulator's host GPU pipe, 2 is SwiftShader)
            // (there libEGL takes it from /vendor/lib64/egl, and without that directory puts the flag back to 0)
            val ss = if (File(paths.root, kernel.eglLib).isFile) kernel.gles else "0"
            // (32-bit programs read it as 0 through the runner: they only have the GLES 1 renderer)
            overrides["ro.kernel.qemu.gles"] = ss; overrides["qemu.gles"] = ss
            // 4.x with our GL bridge for 32-bit programs: the window toolkit draws on the GPU only when
            // GLES20Canvas.isAvailable() - with ro.kernel.qemu set, that is ro.kernel.qemu.gles == 1
            // (frameworks/base core/jni/android_view_GLES20Canvas.cpp 4.0-4.4). With 0 every app of 4.x drew its
            // views through Skia on the CPU (TouchWiz home 46 fps, gfxinfo: no display lists). The loader of libEGL
            // keeps reading qemu.gles (Loader.cpp checkGlesEmulationStatus), which stays as it is; a loader that
            // takes the emulation driver finds the bridge under that name too. (4.0: HWUI through the bridge draws
            // stripes - xm7k64 02.11 - left on the CPU until that is found)
            if (img.abi != "arm64-v8a" && img.api in 16..22 && s.gpu && s.hwui && open32Bridge(engine, img, paths.dir)) {
                overrides["ro.kernel.qemu.gles"] = "1"
                val egl = File(paths.root, "system/lib/egl")
                val emu = File(egl, "libGLES_emulation.so")
                if (File(egl, "libGLES_aemu.so").isFile && !emu.exists()) runCatching { android.system.Os.symlink("libGLES_aemu.so", emu.path) }
            }
        }
        var glBridge = false
        var glInfo = ""
        if (engine == Engine.N64 && kernel.gpuBridge64 && s.gpu && File(paths.root, "${kernel.eglDir}/libGLES_aemu.so").isFile) {
            val r = GlHost.start(paths.socket("aemu_gl").absolutePath)
            log("GPU bridge: $r")
            glBridge = r.startsWith("ok"); glInfo = r
        }
        // Android 1.x: the emulator's GLES 1 driver (libhEGL.so or libhgl.so, TreeFixer) talks to the same host renderer
        if (engine == Engine.N64 && img.api <= 4 && s.gpu && listOf("libhEGL.so", "libhgl.so").any { n ->
                runCatching { String(File(paths.root, "system/lib/$n").readBytes(), Charsets.ISO_8859_1).contains("AEmulator GPU bridge") }.getOrDefault(false) }) {
            paths.socket("aemu_gl").parentFile?.mkdirs()
            val r = GlHost.start(paths.socket("aemu_gl").absolutePath)
            log("GPU bridge (GLES 1 of 1.x): $r")
        }
        // 32-bit engine, test switch "glopen" (TreeFixer put the open bridge in front of the engine's own): programs'
        // contexts go to the same host renderer as on the 64-bit engine; the file's text is the mode (default 2: here the
        // "software" driver under the bridge is the engine's own, which has no texture EGLImages, and WebView's
        // threads share their tiles through those - in mode 1 they stay on it and a page draws only its background)
        val open32Gl = open32Bridge(engine, img, paths.dir)   // (TreeFixer.openGl32)
        if ((open32Gl || (engine == Engine.KK && File(paths.dir, "glopen").isFile)) && s.gpu && File(paths.root, "system/lib/egl/libGLES_split.so").isFile) {
            paths.socket("aemu_gl").parentFile?.mkdirs()
            val r = GlHost.start(paths.socket("aemu_gl").absolutePath)
            log("open GPU bridge (32-bit): $r")
            if (r.startsWith("ok")) overrides["debug.aemu.gl"] = if (open32Gl) "${open32Mode(img, paths.dir)}" else runCatching { File(paths.dir, "glopen").readText().trim() }.getOrDefault("").takeIf { it.isNotEmpty() } ?: "2"
        }
        // 7.0+ libEGL ignores egl.cfg and takes the first libGLES_*.so it finds; without the bridge: the software one
        if (img.api >= 24 && !s.gpu) {
            overrides["ro.hardware.egl"] = "android"
        }
        // ro.boot.hardware comes from the boot loader on a phone (androidboot.hardware); ColorOS tells Qualcomm from
        // MediaTek by it (OplusTelephonyManager: starts with "qcom" or "mt"), and with neither its Telecom has no SoC
        // side at all: system_server dies in CallAudioManager. Named as the firmware's own init.<hardware>.rc is.
        if (engine == Engine.N64 && kernel.bootHardwareProp) {
            val hw = File(paths.root, "vendor/etc/init/hw").list().orEmpty().mapNotNull { Regex("^init[.](qcom|mt[0-9]+[a-z]*)[.]rc$").find(it)?.groupValues?.get(1) }.firstOrNull()
            if (hw != null) overrides["ro.boot.hardware"] = hw
        }
        overrides["ro.sf.lcd_density"] = s.density.toString()
        overrides["qemu.sf.lcd_density"] = s.density.toString()
        // ro.sf.hwrotation describes how the board's panel is mounted (SurfaceFlinger/DisplayHardware 4.x turns every
        // frame by it: Samsung tablets ship a portrait 800x1280 panel with 90). The emulated panel is mounted as the
        // screen is set up, so it is 0, just as the density above is the emulated panel's own.
        overrides["ro.sf.hwrotation"] = "0"
        // MIUI 10 forces the density of the phone's own panel from this one (440 on a 540-pixel screen)
        if (listOf("system/build.prop", "vendor/build.prop").any { runCatching { File(paths.root, it).readText() }.getOrDefault("").contains("persist.miui.density_v2") }) overrides["persist.miui.density_v2"] = s.density.toString()
        overrides["ro.aemu.host"] = if (engine == Engine.N64) "native" else "qemu-user"
        if (engine == Engine.N64) {
            // the phone runs arm64 code only: the 32-bit zygote and its apps go through qemu-arm when it is there
            // (a whole 32-bit firmware, open32, keeps its own zygote and ABI list)
            if (img.abi != "arm64-v8a") {
            } else if (n64Has32) {
                overrides["ro.zygote"] = "zygote64_32"
                overrides["ro.product.cpu.abilist"] = "arm64-v8a,armeabi-v7a,armeabi"
                overrides["ro.product.cpu.abilist64"] = "arm64-v8a"
                overrides["ro.product.cpu.abilist32"] = "armeabi-v7a,armeabi"
            } else {
                overrides["ro.zygote"] = "zygote64"
                overrides["ro.product.cpu.abilist"] = "arm64-v8a"
                overrides["ro.product.cpu.abilist64"] = "arm64-v8a"
                overrides["ro.product.cpu.abilist32"] = ""
            }
        }
        overrides["dalvik.vm.execution-mode"] = if (s.jit) "int:jit" else "int:fast"
        if (s.lowRam || s.ramMb in 1..768) overrides["ro.config.low_ram"] = "true"
        if (s.ramMb > 0) {
            // Dalvik heap sized from the guest RAM budget (like a real device of that class)
            val heap = (s.ramMb / 4).coerceIn(32, 512)
            overrides["dalvik.vm.heapsize"] = "${heap}m"
            overrides["dalvik.vm.heapgrowthlimit"] = "${(heap / 2).coerceAtLeast(24)}m"
        }
        if (!s.radio) overrides["ro.radio.noril"] = "true"
        // 0.9-1.x on open32: the GL boot animation in system_server runs PixelFlinger's generated ARM code, on which qemu-arm faults
        if (img.api < 3 && engine == Engine.N64) overrides["debug.sf.nobootanimation"] = "1"
        // 0.9's simulated radio always reports a roaming network: data (and with it the Browser) needs roaming allowed
        if (img.api < 3) overrides["ro.com.android.dataroaming"] = "true"
        if (!s.hwui) overrides["debug.hwui.renderer"] = "skia"
        // the 64-bit engine has GLES only (bridge or SwiftShader), no Vulkan device: hwui of firmwares that default to
        // Vulkan (HyperOS 2, MediaTek) aborts "Assertion failed: !gpuCount" in system_server's RenderThread
        else if (engine == Engine.N64) overrides["debug.hwui.renderer"] = "skiagl"
        // PBO для текстур шрифтов (hwui 4.4+ на GLES3) ломают часть драйверов (Mali) — грузим текстуры напрямую
        if (HostInfo.gpu().contains("mali")) overrides["ro.hwui.use_gpu_pixel_buffers"] = "false"
        if (img.api in 19..20) overrides["persist.sys.dalvik.vm.lib"] = "libdvm.so"
        // зигота 4.4+ заранее открывает EGL; дети после fork наследуют соединение моста, и гостевая
        // библиотека моста уходит в бесконечную рекурсию (падение по стеку в каждом приложении)
        if (img.api >= 19) overrides["ro.zygote.disable_gl_preload"] = "1"
        if (engine == Engine.N64) overrides.putAll(kernel.props)
        if (glBridge) {
            overrides["ro.hardware.egl"] = "aemu"
            if (kernel.bridgeAsEmulation) {
                overrides["ro.kernel.qemu.gles"] = "1"; overrides["qemu.gles"] = "1"
                File(paths.root, "${kernel.eglDir}/libGLES_aemu.so").copyTo(File(paths.root, "${kernel.emulationDir ?: kernel.eglDir}/libGLES_emulation.so"), overwrite = true)
            }
            // which contexts go to the GPU (native/glbridge/guest.c, policy): 3 - everything, the compositor
            // included; a file "glmode" in the image's directory sets another for a test (0: none, 1: programs' own
            // contexts, 2: the window toolkit's as well)
            overrides["debug.aemu.gl"] = File(paths.dir, "glmode").takeIf { it.isFile }?.readText()?.trim()?.takeIf { it.isNotEmpty() } ?: kernel.gpuBridgeMode
            // GLES 3.0, or 3.1 when the phone's contexts have it (the renderer says "ES 3.1 to the guest")
            overrides["ro.opengles.version"] = if (glInfo.contains("ES 3.1 to the guest")) "196609" else "196608"
        }
        // open32 on Dalvik (4.4 and older): its JIT aborts in the assembler under qemu (dvmAbort; in system_server the
        // whole system restarts, a black screen); the fast interpreter instead
        if (open32 && img.api < 21 && !File(paths.dir, "jit32").exists()) overrides["dalvik.vm.execution-mode"] = "int:fast"
        // ART first boot compiles every app with dex2oat under qemu (minutes, single-threaded): by default only
        // skip it (verify-none: ART verifies classes lazily when an app runs) and use several threads; "full compilation" in the settings keeps machine code
        if (img.api >= 21) {
            // (the native arm64 engine too: dex2oat is a 32-bit program there and runs translated; the boot image is
            // still compiled in full, it has its own filter)
            if (!s.fullDexopt) overrides["dalvik.vm.dex2oat-filter"] = "verify-none"
            // a whole 32-bit firmware on the arm64 kernel (open32): interpreted Java under the translator is what makes
            // its UI lag (launcher pages ~10 fps, 66-75 compiled), so new and background compiles make machine code;
            // the first boot stays quick (verify-none) and afterBoot compiles the always-visible apps once
            if (open32) {
                overrides["dalvik.vm.dex2oat-filter"] = "speed"
                for (r in listOf("install", "bg-dexopt", "core-app", "forced-dexopt")) overrides["pm.dexopt.$r"] = "speed"
                for (r in listOf("first-boot", "boot", "ab-ota", "nsys-library")) overrides["pm.dexopt.$r"] = "verify-none"
            }
            // 7.0+: PackageManager picks the filter per reason from pm.dexopt.*
            if (img.api >= 24 && !s.fullDexopt && engine != Engine.N64) for (r in listOf("first-boot", "boot", "install", "bg-dexopt", "ab-ota", "core-app", "forced-dexopt", "nsys-library"))
                overrides["pm.dexopt.$r"] = "verify-none"
            overrides["dalvik.vm.dex2oat-flags"] = "-j" + Runtime.getRuntime().availableProcessors().coerceIn(2, 4)
            // 6.0 relocates the boot image to a random address (patchoat), which fails under qemu: keep it in place
            if (img.api >= 23) {
                overrides["dalvik.vm.extra-opts"] = "-Xnorelocate"
                // installd's dex2oat starts its own runtime: without this it tries patchoat too and fails every app
                overrides["dalvik.vm.dex2oat-flags"] = overrides["dalvik.vm.dex2oat-flags"] + " --runtime-arg -Xnorelocate"
            }
        }
        // порты MIUI правят framework на smali так, что Dalvik-верификатор отвергает классы ядра
        // (зигота падает на VerifyError) — на телефонах они живут с выключенной проверкой байткода
        if (img.skin.contains("MIUI", true) && img.runtime == "dalvik") overrides["dalvik.vm.dexopt-flags"] = "v=n,o=a,m=y"
        // Dalvik's first boot runs dexopt over every apk without an odex, one after another and translated (S III: 88
        // apps, 104 s), and verification is most of it; like ART's verify-none above, unless "full compilation" is on
        else if (img.runtime == "dalvik" && !s.fullDexopt) overrides["dalvik.vm.dexopt-flags"] = "v=n,o=a"
        // MediaTek: звук, камера, радио ждут «NVRAM готов» от nvram_daemon, которого у нас нет
        if (TreeFixer.isMtkAudio(paths.root) || File(paths.root, "system/bin/nvram_daemon").isFile) overrides["nvram_init"] = "Ready"
        // TouchWiz 4.1+: TvOutService and Samsung EDS look for HDMI/MHL hardware and keep crashing or blocking boot
        if (img.api >= 16 && (img.skin.contains("TouchWiz", true) || img.skin.contains("One UI", true))) {
            overrides["ro.tvout.enable"] = "false"
            overrides["ro.SecEDS.enable"] = "false"
        }
        // своя панель кнопок у нас — экранную панель гостя (4.x) прячем, чтобы не дублировать
        overrides["qemu.hw.mainkeys"] = if (s.showNavBar && !(engine == Engine.N64 && kernel.navBarRequired)) "1" else "0"
        TargetRadioLaunch.applyBootProperties(radioBackend, overrides)
        TargetRichTapProperty.apply(img.api, img.model, img.abi, engine, overrides)
        // отладка: run/props.extra — строки ключ=значение поверх всего остального
        File(paths.bin, "props.extra").takeIf { it.isFile }?.readLines()?.forEach { l ->
            val k = l.substringBefore('=').trim()
            if (k.isNotEmpty() && !k.startsWith("#") && l.contains('=')) overrides[k] = l.substringAfter('=').trim()
        }
        // 12+: package lists and settings stay binary XML (the default). An image once started with this set to false
        // (15.10, briefly) keeps the value in its saved properties, and 14 then cannot read its own text files
        // ("illegal attribute name: :strings" in DevicePolicyManagerService): put back explicitly.
        if (engine == Engine.N64) overrides["persist.sys.binary_xml"] = "true"
        overrides["net.dns1"] = "8.8.8.8"
        overrides["net.dns2"] = "1.1.1.1"
        // what a vendor RIL (or its pppd) sets for the data interface it brings up (Stubs.IFACE): 1.x-2.2's
        // PdpConnection takes the servers from net.<iface>.dns1/2, not from the SETUP_DATA_CALL answer, and
        // MobileDataStateTracker copies them over net.dnsN and blanks the rest; without them every lookup waited out
        // bionic's 5 s timeout on a server that is not there
        overrides["net.rmnet0.dns1"] = "8.8.8.8"
        overrides["net.rmnet0.dns2"] = "1.1.1.1"
        // в прошивках старая база часовых поясов — передаём текущее смещение, а не название зоны
        overrides["persist.sys.timezone"] = gmtZone()
        if (img.api < 3) {
            // pre-1.0 libc takes the zone from /data/timezone, not from the property
            runCatching { File(paths.root, "data/timezone").writeText(gmtZone()) }
            // what the SDK emulator's qemu-init.sh sets: telephony simulated in Java (M5 checks "yes"), no ppp
            overrides["ro.radio.noril"] = "yes"
            overrides["ro.radio.use-ppp"] = "no"
            overrides["ro.config.nocheckin"] = "yes"
            overrides["net.eth0.dns1"] = "8.8.8.8"
            overrides["net.gprs.local-ip"] = "10.0.2.15"
        }
        // what init reads after /system/build.prop: the default properties of a system-only image (8.0+) and the
        // vendor's own files (on a Treble image the vendor side is the engine's). Only what the image does not say.
        val later = LinkedHashMap<String, String>()
        for (n in listOf("system/etc/prop.default", "vendor/default.prop", "vendor/build.prop")) runCatching {
            File(paths.root, n).takeIf { it.isFile }?.let { f -> PropArea.parseProps(f.readText()).forEach { (k, v) -> later.putIfAbsent(k, v) } }
        }
        // A firmware's own vendor built without VNDK (15+: no ro.vndk.version, the system has the unversioned LLNDK
        // list) must keep it unset - linkerconfig then gives the vendor the current LLNDK set; with the engine's
        // "34" it took VNDK 34's list and vendor libbinder lost libapexsupport.so.
        val vndkLessVendor = File(paths.root, "vendor/build.prop").isFile && !later.containsKey("ro.vndk.version") &&
            File(paths.root, "system/etc/llndk.libraries.txt").isFile
        if (engine == Engine.N64) kernel.propDefaults.forEach { (k, v) ->
            if (!(vndkLessVendor && k == "ro.vndk.version")) later.putIfAbsent(k, v)
        }
        if (engine == Engine.N64 && kernel.derivedIdentity) deriveIdentity(later)
        // a firmware whose build.prop lost ro.product.manufacturer (Build.MANUFACTURER, API 4; vendor apps check it):
        // the maker that signed the platform, from framework-res.apk's certificate (its O=, without the legal form)
        if (img.api >= 4 && !later.containsKey("ro.product.manufacturer") && listOf("system/build.prop", "vendor/build.prop", "default.prop")
                .none { n -> runCatching { File(paths.root, n).readText().contains("ro.product.manufacturer=") }.getOrDefault(false) })
            runCatching { platformMaker() }.getOrNull()?.let { later["ro.product.manufacturer"] = it }
        // Without Treble's own check (it needs SELinux), system_server of 12 compares the partitions' fingerprints
        // and shows "There's an internal problem with your device" when the vendor's is of another build - which it
        // is on every phone updated from an earlier release.
        // init reads the vendor's properties after the system's and a later value wins (but for ro.*): the generic
        // system part of such a firmware says a 36 MB Java heap, the vendor's file has the phone's real sizes
        if (engine == Engine.N64 && kernel.vendorHeapProps) runCatching {
            PropArea.parseProps(File(paths.root, "vendor/build.prop").readText()).forEach { (k, v) ->
                if (k.startsWith("dalvik.vm.heap")) overrides.putIfAbsent(k, v)
            }
        }
        if (engine == Engine.N64 && kernel.mediaProfilesFallback) runCatching {
            val etc = File(paths.root, "vendor/etc")
            // (Qualcomm's libmedia opens _vendor, or the chip's name on bengal: _khaje, _scuba)
            listOf("media_profiles_V1_0.xml", "media_profiles.xml").firstOrNull { File(etc, it).isFile }?.let { have ->
                for (name in listOf("vendor", "khaje", "scuba")) File(etc, "media_profiles_$name.xml").let {
                    if (!it.exists()) runCatching { android.system.Os.symlink(have, it.absolutePath) }
                }
            }
        }
        if (engine == Engine.N64 && kernel.oneFingerprint) runCatching {
            PropArea.parseProps(File(paths.root, "system/build.prop").readText())["ro.system.build.fingerprint"]?.let { fp ->
                overrides["ro.vendor.build.fingerprint"] = fp; overrides["ro.bootimage.build.fingerprint"] = fp
            }
        }
        if (!props.prepare(overrides, later, if (engine == Engine.N64) kernel.propAreaSize else PropArea.SIZE)) error("property area not ready")
        if (img.api == 36 && img.model == "NX809J") {
            checkNotNull(props.area).applyTargetBootProperties()
        }
        fixer.skipPreBoot(props)
        fixer.noScreenSleep()
        props.onSet = { k, v -> onProp(k, v) }
        props.onCtl = { start, svc -> onCtl(start, svc) }

        // 3. службы хоста
        props.serve()
        if (img.api < 3) props.serveAbstract()
        input.rateHz = s.touchHz
        input.mtMode = if (img.api < 5) 4 else s.mtMode
        input.wide = engine == Engine.N64 && img.abi == "arm64-v8a"     // (a 32-bit firmware reads 16-byte events)
        input.serve()
        frames.serve()
        when (radioBackend) {
            TargetRadioLaunch.Backend.LEGACY -> ril.serve()
            TargetRadioLaunch.Backend.DISABLED -> log("radio: emulation disabled in settings")
            TargetRadioLaunch.Backend.AIDL -> log("radio: target AIDL service selected")
        }
        vold.serve()
        // у vold производителей бывают дополнительные сокеты (Samsung: usbstorage, enc_report) — отвечаем «ладно»
        runCatching {
            // 7.0+ keeps vold (with its cryptd socket) in /system/etc/init
            val rcFiles = listOf(paths.root, File(paths.root, "system/etc/init"))
                .flatMap { d -> d.listFiles()?.filter { it.isFile && it.name.endsWith(".rc") } ?: emptyList() }
            val rc = InitPlan.parse(rcFiles)
            rc.services["vold"]?.sockets?.keys?.filter { it != "vold" }?.forEach { name ->
                VoldStub(paths, img.sdcardPath, ::log, name).also { it.serve(); extraStubs.add(it) }
            }
        }
        // MediaTek: libaudioflinger сам работает с устройством /dev/eac (звуковой драйвер MTK) и читает из
        // него — наш канал звука под тем же именем его вешает, а за ним mediaserver и всю систему.
        // Для MTK /dev/eac ведёт в /dev/null: без звука, но без зависания.
        if (TreeFixer.isMtkAudio(paths.root)) {
            runCatching {
                val eac = audio.fifo
                if (!isLinkTo(eac, "/dev/null")) { eac.delete(); android.system.Os.symlink("/dev/null", eac.absolutePath) }
                // голосовой канал модема (CCCI): без него AudioMTKHardware бесконечно ждёт модем
                for (n in listOf("ccci_pcm_rx", "ccci_pcm_tx")) {
                    val f = File(paths.root, "dev/$n")
                    if (!isLinkTo(f, "/dev/null")) { f.delete(); android.system.Os.symlink("/dev/null", f.absolutePath) }
                }
            }
            // our HAL replaces the MTK one (TreeFixer) and plays through its own channel
            audio.makeFifo(); audio.start()
            log("audio: MediaTek, emulator HAL via /dev/aemu_pcm")
        } else { audio.makeFifo(); audio.start() }
        events.start()
        lights.start()
        sensors.start()
        mic.start()
        camera.start()
        venc.start()
        overlays.start()
        if (img.api >= 21) logd.start()
        if (img.api >= 14) runCatching { dns.ensure() }
        // the RAM the guest is told it has (its /proc/meminfo, scaled by the kernel): the framework sizes its background by it
        if (engine == Engine.N64) runCatching {
            val f = File(paths.root, "dev/.aemu-ram")
            if (kernel.ramBudget) f.writeText((GuestRunner.guestRamMb(img.api, s.ramMb) * 1024L).toString()) else f.delete()
        }
        // internal storage size chosen in the image settings (kernel 23 reports it in statfs)
        // Without a choice, Android up to 2.3 sees at most 2000 MB: StatFs gives int blocks there and code of that time
        // multiplies them by the block size as int (Sony's 1.6 keyguard: 62 GB free wrapped to a negative size →
        // "Memory full"); phones of that time had /data well under 2 GB
        val diskMb = if (s.diskMb > 0) s.diskMb else if (img.api <= 10) 2000 else 0
        runCatching { File(paths.root, "dev/.aemu-disk").let { if (diskMb > 0) it.writeText(diskMb.toString()) else it.delete() } }
        lmk = GuestLmk(paths.root, s.lowRam, ::log, s.ramMb, cgroupSockets = open32).also { it.start() }
        val netCfg = if (s.netProxy) net.start() else GuestRunner.NetConfig()
        runner.sdcardHost = sd
        val r = GuestRunner(paths, img, netCfg.copy(glPath = if (s.gpu) "/dev/socket/gl" else null)).also {
            it.processStarter = ::startExternalProcess
            it.sdcardHost = sd
            // отладка: файл run/binder.verbose включает полную трассировку binder в binder-warn.log
            it.binderVerbose = File(paths.bin, "binder.verbose").exists()
            it.userQemuArgs = s.qemuArgs
        }
        runnerRef = r
        Keeper.hold(ctx, img.name)

        bootAt = System.currentTimeMillis()
        bootDoneAt = 0
        screenUpAt = 0
        everBooted = false; zygoteRestarts = 0; zygoteCrashes.clear()
        setState(State.BOOTING)

        // 4. binder
        paths.binderSock.delete()
        paths.hwbinderSock.delete()
        paths.vndbinderSock.delete()
        paths.creds.let { d -> d.listFiles()?.forEach { it.delete() }; d.mkdirs() }
        startBinder()
        if (!waitFor("binder socket", 15_000) { paths.binderSock.exists() }) error("binderd failed to start")
        if (engine == Engine.N64 && kernel.treble) {
            waitFor("hwbinder socket", 5_000) { paths.hwbinderSock.exists() }
            waitFor("vndbinder socket", 5_000) { paths.vndbinderSock.exists() }
        }

        // 11+: the linker takes its library namespaces (which APEX module gives which library to whom) from
        // /linkerconfig, written at every boot by the firmware's own generator; init runs it before any service
        if (engine == Engine.N64 && File(paths.root, kernel.linkerConfigTool).isFile) runCatching {
            apexInfoList()
            File(paths.root, "linkerconfig").mkdirs()
            // (a generator that writes the one-namespace configuration of a device before Treble for
            // ro.treble.enabled=false - MIUI 12.5's does, and no vendor program finds its VNDK - reads "true" here)
            val own = if (kernel.trebleLinkerConfig) props.privateCopy("linkerconfig", mapOf("ro.treble.enabled" to "true")) else null
            val (code, out) = r.run(listOf("/" + kernel.linkerConfigTool, "--target", "/linkerconfig"), 30_000,
                if (own != null) mapOf("AEMU64_PROPS_FILE" to own.absolutePath) else emptyMap())
            if (code != 0 || !File(paths.root, "linkerconfig/ld.config.txt").isFile) log("linkerconfig: exit $code ${out.trim().let { if (it.length > 700) it.take(400) + " ... " + it.takeLast(300) else it }}")
        }

        r.derivedExports = emptyMap()
        if (engine == Engine.N64 && File(paths.root, "apex/com.android.sdkext/bin/derive_classpath").isFile) runCatching {
            val out = File(paths.root, "data/system/environ/classpath")
            out.parentFile?.mkdirs(); out.delete()
            val (code, msg) = r.run(listOf("/apex/com.android.sdkext/bin/derive_classpath", "/data/system/environ/classpath"), 30_000)
            if (code != 0 || !out.isFile) log("derive_classpath: exit $code ${msg.trim().takeLast(300)}")
            else r.derivedExports = out.readLines().mapNotNull { l -> l.trim().split(' ', limit = 3).takeIf { it.size == 3 && it[0] == "export" }?.let { it[1] to it[2] } }.toMap()
        }

        // 5. службы гостя по плану из init.rc прошивки
        val plan = servicePlan()
        if (aidlRadio != null) TargetRadioLaunch.requireServiceOrder(plan)
        val managedVibrationBinder = synchronized(procs) { procs["binderd"] } as? ManagedGuestProcess
        val directControl = if (directVibrationEnabled) runCatching { HostVibrationDevice.fromContext(ctx) }.getOrNull() else null
        var directPending = false
        if (directVibrationEnabled && DirectVibrationBoot.validServiceOrder(plan) &&
            DirectVibrationBoot.eligible(true, recoveryMode, img.api, img.model, img.abi, engine,
                managedVibrationBinder != null, directControl != null,
                HostNative.available && VibrationBinderExperimentalNative.available)) {
            try {
                directVibrationBlockQti = true
                directPending = TargetVibrationDeclaration.prepare(paths.root, img.api, img.model, img.abi, engine, true)
                if (!directPending) directVibrationBlockQti = false
            } catch (failure: Exception) {
                directVibrationBlockQti = false
                // No ServiceManager has started yet; remove only our matching fragment.
                TargetVibrationDeclaration.prepare(paths.root, img.api, img.model, img.abi, engine, false)
                val stage = (failure as? VibrationDeclarationStageException)?.stage?.name ?: failure.javaClass.simpleName
                log("vibration: preparation unavailable ($stage)")
            }
        }
        var glDone = false
        // (debugging, see Kernel64.syncLog: only the first N services are started, then the guest is left as it is)
        val stopAfter = if (engine == Engine.N64 && kernel.syncLog) runCatching { File(paths.root, "dev/.aemu-stop-after").readText().trim().toInt() }.getOrNull() else null
        if (stopAfter != null) log("stop-after $stopAfter of: " + plan.filter { !it.onDemand }.mapIndexed { i, s -> "$i ${s.name}" }.joinToString())
        var started = 0
        for (svc in plan) {
            if (stopping) return
            if (svc.onDemand) { props.put("init.svc.${svc.name}", "stopped"); continue }
            if (stopAfter != null && started++ >= stopAfter) {
                log("stop-after $stopAfter: holding before ${svc.name}")
                Thread.sleep(60_000)
                log("stop-after $stopAfter: 60 s passed, alive: ${alive().joinToString()}")
                return
            }
            // GL-мост должен ждать гостя до SurfaceFlinger/zygote
            if (!glDone && (svc.name == "surfaceflinger" || svc.name == "zygote" || svc.name == "bootanim")) {
                glUp(); glDone = true
            }
            // 10+: the package manager requires apexd's binder service, which is there once apexd reports "ready"
            if (svc.name == "zygote" && engine == Engine.N64 && plan.any { it.name == "apexd" })
                waitFor("apexd", 15_000) { props.get("apexd.status") == "ready" }
            if (svc.name == "zygote" && aidlRadio != null) {
                log("radio: requesting AIDL service start; framework discovery not verified")
                startService(aidlRadio)
            }
            // 4.2+: заглушка bluetooth_manager до зиготы (system_server с ro.kernel.qemu=1 свою не поднимает)
            if (svc.name == "zygote" && img.api >= 9 && engine != Engine.GB && File(paths.root, "system/framework/aemu-stubs.jar").isFile) {
                // 2.3–4.1 look up "bluetooth" instead; without it BluetoothAdapter is null and the vendor's Bluetooth apps crash
                val btName = if (img.api >= 17) "bluetooth_manager" else "bluetooth"
                startService(GuestService("aemu-bt", listOf("/system/bin/app_process",
                    "-Djava.class.path=/system/framework/aemu-stubs.jar", "/system/bin", "app.aemu.stub.BtStub", btName), optional = true))
            }
            // (well before zygote: apexd of 10+ looks for vold first and only then registers its own service, which the
            // package manager requires)
            if (svc.name == "surfaceflinger" && engine == Engine.N64 && kernel.binderVold && File(paths.root, "system/framework/aemu-stubs.jar").isFile) {
                killOurs(synchronized(procs) { procs.remove("aemu-vold") })
                startService(GuestService("aemu-vold", listOf("/system/bin/app_process",
                    "-Djava.class.path=/system/framework/aemu-stubs.jar", "/system/bin", "app.aemu.stub.VoldStub") + (if (kernel.volumeAtUserStart) listOf("userstart") else emptyList()), optional = true))
            }
            // 5.0–7.x: a registered network (by the firmware's API: open32 runs 2.3–4.4 on kernel 23, where NetworkAgent
            // does not exist and the stub died 137/139), otherwise ConnectivityService says "no active network" and browsers stay offline
            if (svc.name == "zygote" && img.api >= 21 && (if (engine == Engine.N64) kernel.registeredNetwork else img.api in 21..25) && engine != Engine.GB && File(paths.root, "system/framework/aemu-stubs.jar").isFile) {
                killOurs(synchronized(procs) { procs.remove("aemu-net") })
                startService(GuestService("aemu-net", listOf("/system/bin/app_process",
                    "-Djava.class.path=/system/framework/aemu-stubs.jar", "/system/bin", "app.aemu.stub.NetStub") + (if (Kernel64.forApi(img.api).ethernetStandIn) listOf("ethernet") else emptyList()), optional = true))
            }
            if (svc.name == "zygote" && engine == Engine.N64 && kernel.apexStandIn && File(paths.root, "system/framework/aemu-stubs.jar").isFile) {
                apexStandInList()
                killOurs(synchronized(procs) { procs.remove("aemu-apex") })
                startService(GuestService("aemu-apex", listOf("/system/bin/app_process",
                    "-Djava.class.path=/system/framework/aemu-stubs.jar", "/system/bin", "app.aemu.stub.ApexStub"), optional = true))
            }
            // services the firmware's programs call unchecked and nothing here registers
            if (svc.name == "zygote" && engine == Engine.N64 && File(paths.root, "system/framework/aemu-stubs.jar").isFile) {
                val names = Kernel64.forApi(img.api).standInServices.filter { (name, user) ->
                    runCatching { String(File(paths.root, user.substringBefore('#')).readBytes(), Charsets.ISO_8859_1).contains(user.substringAfter('#', name)) }.getOrDefault(false) }.keys
                killOurs(synchronized(procs) { procs.remove("aemu-standin") })
                if (names.isNotEmpty()) startService(GuestService("aemu-standin", listOf("/system/bin/app_process",
                    "-Djava.class.path=/system/framework/aemu-stubs.jar", "/system/bin", "app.aemu.stub.StandIn") + names, optional = true))
            }
            startService(svc)
            if (directPending && svc.name == "servicemanager") {
                directPending = false
                try {
                    val manager = synchronized(procs) { procs["servicemanager"] }
                        ?: error("ServiceManager did not start")
                    check(awaitDirectVibrationManager(r, manager)) { "ServiceManager unavailable" }
                    startDirectVibration(managedVibrationBinder!!, directControl!!, manager)
                    log("vibration: host bridge registration acknowledged; discovery and Guest permissions unverified")
                } catch (failure: Exception) {
                    if (stopping) return
                    log("vibration: host bridge unavailable (${failure.javaClass.simpleName}); restoring boot")
                    DirectVibrationBoot.rollback(
                        stopBridge = { closeDirectVibration()?.let { throw it } },
                        stopManager = { stopEarlyVibrationProcess("servicemanager") },
                        stopBinder = { stopEarlyVibrationProcess("binderd") },
                        removeDeclaration = { TargetVibrationDeclaration.prepare(paths.root, img.api, img.model, img.abi, engine, false) },
                        restartBinder = {
                            paths.binderSock.delete()
                            spawnMainBinder()
                            check(waitFor("binder socket", 15_000) { paths.binderSock.exists() }) { "binderd failed after vibration rollback" }
                        },
                        restartManager = { startService(svc) },
                    )
                    directVibrationBlockQti = false
                }
            }
            svc.waitSocket?.let { sock -> waitFor("socket $sock", 20_000) { paths.socket(sock).exists() } }
            if (svc.delayMs > 0) Thread.sleep(svc.delayMs)
        }
        if (!glDone) glUp()
        log("system started: ${alive().joinToString()}")
        // M3/M5 set no boot property at all: their SurfaceFlinger's "Boot is finished" in the guest log is the sign
        if (img.api < 3) Thread {
            val f = File(paths.root, "dev/log/main"); val mark = "Boot is finished".toByteArray()
            val start = bootAt
            val from = f.length()                 // (the log keeps earlier boots)
            while (bootDoneAt == 0L && bootAt == start && state != State.FAILED && state != State.STOPPED) {
                val hit = runCatching { f.readBytes().let { d -> (minOf(from, d.size.toLong()).toInt()..d.size - mark.size).any { i -> mark.indices.all { d[i + it] == mark[it] } } } }.getOrDefault(false)
                if (hit) { booted(); break }
                Thread.sleep(1000)
            }
        }.apply { isDaemon = true }.start()
        watchdog()
    }

    @Volatile private var runnerRef: GuestRunner? = null
    val guestRunner: GuestRunner get() = runnerRef ?: runner

    private fun spawnMainBinder() {
        val env = if (File(paths.bin, "binder.verbose").exists()) mapOf("AEMU_BINDER_VERBOSE" to "1") else emptyMap()
        val displayEnv = if (img.api == 36 && File(paths.bin, "binder.display.trace").isFile)
            env + ("AEMU_BINDER_DISPLAY_TRACE" to "1") else env
        val txnEnv = if (img.api == 36 && File(paths.bin, "binder.txn.trace").isFile)
            displayEnv + ("AEMU_BINDER_TXN_TRACE" to "1") else displayEnv
        val mainEnv = if (img.api == 36 && File(paths.bin, "binder.media.main.trace").isFile)
            txnEnv + ("AEMU_MEDIA_BINDER_TRACE" to "main") else txnEnv
        spawn("binderd", listOf(paths.nativeBin(kernel.binderd).absolutePath, paths.binderSock.absolutePath), mainEnv)
    }

    private fun startBinder() {
        val sock = paths.binderSock.absolutePath
        when (engine) {
            Engine.KK -> spawn("binderd", listOf(paths.nativeBin(engine.binderd).absolutePath, "-s", sock), emptyMap())
            Engine.N64 -> {
                val env = if (File(paths.bin, "binder.verbose").exists()) mapOf("AEMU_BINDER_VERBOSE" to "1") else emptyMap()
                val bin = paths.nativeBin(kernel.binderd).absolutePath
                spawnMainBinder()
                if (kernel.treble) {
                    val hwEnv = if (img.api == 36 && File(paths.bin, "binder.media.hw.trace").isFile)
                        env + ("AEMU_MEDIA_BINDER_TRACE" to "hw") else env
                    spawn("hwbinderd", listOf(bin, paths.hwbinderSock.absolutePath), hwEnv)
                    spawn("vndbinderd", listOf(bin, paths.vndbinderSock.absolutePath), env)
                    log("· Treble hwbinder and vndbinder daemons started")
                }
            }
            Engine.GB -> if (File(paths.bin, "binderd.kk").exists()) {
                // эксперимент: более новый binderd движка KK
                spawn("binderd", listOf(paths.nativeBin(Engine.KK.binderd).absolutePath, "-s", sock), emptyMap())
            } else {
                Slot.start(ctx, BinderSlot::class.java, listOf("binderd", "-s", sock), emptyMap(), paths.log("binderd"))
                log("· binderd started (:binder service)")
            }
        }
    }

    private fun glUp() {
        if (!settings.gpu) { log("GPU bridge off, software rendering"); return }
        val (w, h) = if (engine == Engine.GB) 480 to 800 else settings.width to settings.height
        paths.glSock.parentFile?.mkdirs()
        paths.glSock.delete()
        when (engine) {
            Engine.N64 -> if (img.abi != "arm64-v8a" && open32Compositor(engine, img, paths.dir)) {
                // the open bridge serves every context and the display itself (guest.c own_egl); the compositor's
                // frames reach fb0 through the firmware's framebuffer HAL
                log("GPU bridge: open (no glserverd)")
            } else if (img.abi != "arm64-v8a") {
                // a whole 32-bit firmware (open32): the 4.4 engine's GL server draws its frames into fb0, as on that engine
                log("GPU bridge: standalone glserverd (open32)")
                spawn("glserverd", listOf(paths.nativeBin(Engine.KK.glserverd).absolutePath,
                    "-s", paths.glSock.absolutePath, "-fb", paths.fb.absolutePath,
                    "-notify", paths.frameSock.absolutePath, "-w", "$w", "-h", "$h"), glServerEnv())
                waitFor("GL bridge socket", 10_000) { paths.glSock.exists() }
            } else {
                log("native arm64: graphics are served by the engine's own HALs")
                // ...except for 32-bit programs, which draw through the 4.4 engine's GPU bridge (its frame file is not shown)
                if (kernel.gpuBridge32 != null && n64Has32) {
                    spawn("glserverd", listOf(paths.nativeBin(Engine.KK.glserverd).absolutePath,
                        "-s", paths.glSock.absolutePath, "-fb", File(paths.bin, "gl32.fb").absolutePath, "-w", "$w", "-h", "$h"), glServerEnv())
                    waitFor("GL bridge socket", 10_000) { paths.glSock.exists() }
                }
            }
            Engine.GB -> {
                Slot.start(ctx, GlSlot::class.java,
                    listOf("glserverd", "-s", paths.glSock.absolutePath, "-fb", paths.fb.absolutePath, "-w", "$w", "-h", "$h"),
                    emptyMap(), paths.log("glserverd"))
                waitFor("GL bridge socket", 10_000) { paths.glSock.exists() }
            }
            Engine.KK -> {
                if (surface == null) waitFor("screen surface", 10_000) { surface != null }
                val sf = surface
                HostNative.relaxFdsanSafe()
                // 5.0+: hwui's GL stream has crashed the in-app bridge (SIGSEGV in serve_one_raw took the whole app down);
                // the standalone glserverd keeps such a crash inside its own process
                if (img.api < 21 && sf != null && GlBridge.start(sf, paths.glSock.absolutePath, paths.log("glbridge").absolutePath, w, h, false)) {
                    log("★ GPU bridge running in-app: frames render straight to the surface")
                    waitFor("GL bridge socket", 5_000) { paths.glSock.exists() }
                    return
                }
                log(if (img.api >= 21) "GPU bridge: standalone glserverd" else "in-app GPU bridge failed, falling back to standalone glserverd")
                spawn("glserverd", listOf(paths.nativeBin(engine.glserverd).absolutePath,
                    "-s", paths.glSock.absolutePath, "-fb", paths.fb.absolutePath,
                    "-notify", paths.frameSock.absolutePath, "-w", "$w", "-h", "$h"), glServerEnv())
                waitFor("GL bridge socket", 10_000) { paths.glSock.exists() }
            }
        }
    }

    /** the 4.4 engine's GL server reads every GL call of a client with its own system calls; native/glbuf reads the
     *  client sockets in large chunks instead (7.1 GLES 2/3 scenes were bound by those reads) */
    private fun glServerEnv(): Map<String, String> {
        val buf = paths.nativeBin("libglbuf.so")
        return if (buf.isFile) mapOf("LD_PRELOAD" to buf.absolutePath) else emptyMap()
    }

    /** guest files this kernel ships (asset names) */
    private val engineFiles: Set<String> by lazy { listOf("engines/n64-${kernel.tag}", kernel.gsiAssets).flatMap { runCatching { EngineAssets.list(ctx, it) }.getOrNull().orEmpty().asList() }.toSet() }

    private val n64Has32: Boolean get() = paths.nativeBin("libqemu_arm.so").canExecute() &&
            File(paths.root, "system/bin/app_process32").isFile

    val glInApp: Boolean get() = engine == Engine.KK && GlBridge.running()

    private fun startService(svc0: GuestService, propsFile: File? = null) {
        if (DirectVibrationBoot.skipQti(directVibrationBlockQti, img.api, img.model, img.abi, engine, svc0.argv)) {
            props.put("init.svc.${svc0.name}", "stopped")
            return
        }
        // Older saved plans and explicit service starts also pass through here.
        if (InitPlan.isZteOffModeCharger(svc0.argv)) return
        if (InitPlan.isIsolatedQtiBluetoothHal(img.api, svc0.argv)) return
        // The second zygote is of the other word size than the first. A firmware that ships all three zygote rc files
        // and does not name one (GSI 10: no ro.zygote) can leave both 64-bit in the plan - then 32-bit apps have no
        // zygote and system_server dies at boot ("Unsupported zygote ABI: armeabi-v7a").
        fun is64(path: String?): Boolean = path != null && runCatching {
            File(paths.root, path.removePrefix("/")).inputStream().use { i -> ByteArray(5).also { i.read(it) }[4].toInt() == 2 }
        }.getOrDefault(false)
        val svc = if (svc0.name == "zygote_secondary" && is64(svc0.argv.firstOrNull()) &&
            is64(img.services.firstOrNull { it.name == "zygote" }?.argv?.firstOrNull()) &&
            File(paths.root, "system/bin/app_process32").isFile)
            svc0.copy(argv = listOf("/system/bin/app_process32") + svc0.argv.drop(1)) else svc0
        if (!File(paths.root, svc.argv.first().removePrefix("/")).isFile) {
            log("· ${svc.name}: ${svc.argv.first()} missing, skipping")
            return
        }
        if (engine == Engine.N64 && ((svc.name == "zygote_secondary" && !n64Has32) || svc.argv.first().substringAfterLast('/') in N64_SKIP)) return
        if (engine == Engine.N64 && !paths.nativeBin("libqemu_arm.so").canExecute() && !runsNatively(File(paths.root, svc.argv.first().removePrefix("/")))) {
            log("· ${svc.name}: 32-bit program, skipped by the native arm64 engine")
            return
        }
        val extra = LinkedHashMap<String, String>()
        for ((sock, mode) in svc.sockets) extra["DHD_SOCK_$sock"] = "${paths.socket(sock).absolutePath},${if (engine == Engine.N64) mode else mode.substringBefore(',')}"
        if (svc.uid != 0) { extra["DHD_UID"] = svc.uid.toString(); extra["DHD_GID"] = svc.gid.toString() }
        var props = propsFile
        // звук: с ro.kernel.qemu=1 mediaserver берёт эмуляторный вывод в /dev/eac
        // 2.2-2.3 AudioPolicyService (frameworks/base AudioPolicyService.cpp): with ro.kernel.qemu it builds
        // AudioPolicyManagerBase. Galaxy S I9000 2.1/2.2 libaudioflinger has neither that class nor
        // AudioHardwareGeneric, the emulator branch leaves mpPolicyManager null and mediaserver dies (SIGSEGV)
        // (the global copy carries ro.kernel.qemu=1 too; the check is property_get()'s length, so "0" still counts: empty)
        if (svc.name == "mediaserver")
            props = this.props.privateCopy("media", mapOf("ro.kernel.qemu" to
                if (img.api in 8..10 && !hasPolicyBase() || TreeFixer.legacyAudioWantsNoQemu(paths.root)) "" else "1"))
        val argv = if (svc.name == "zygote" && !settings.jit && img.runtime == "dalvik")
            svc.argv.take(1) + "-Xint:fast" + svc.argv.drop(1) else svc.argv
        // отладка: файл run/strace.<служба> включает трассировку системных вызовов qemu
        if (File(paths.bin, "strace.${svc.name}").exists()) extra["QEMU_STRACE"] = "1"
        // отладка: run/gdb.<служба> (внутри — порт) — qemu ждёт отладчик (gdbstub) на этом порту телефона
        File(paths.bin, "gdb.${svc.name}").takeIf { it.exists() }?.let { f -> extra["QEMU_GDB"] = f.readText().trim().ifEmpty { "1234" } }
        if (engine == Engine.N64 && props != null) extra["AEMU64_PROPS_FILE"] = props.absolutePath
        // the engine's own 64-bit HAL service over a vendor's firmware: its libraries are the engine's copy of its VNDK
        if (engine == Engine.N64) kernel.ownVndkDir?.let { dir ->
            val name = svc.argv.first().removePrefix("/").replace('/', '#')
            if (name.startsWith("vendor#bin#") && name !in kernel.firmwareOwn && File(paths.root, dir).isDirectory &&
                name in engineFiles && runsNatively(File(paths.root, svc.argv.first().removePrefix("/")))) extra["AEMU64_LIBPATH"] = "/$dir"
        }
        if (engine == Engine.N64) kernel.ownLibDirs[svc.argv.first().removePrefix("/").replace('/', '#')]?.let { dir ->
            // (several directories: "a:b", searched in that order)
            val dirs = dir.split(':').filter { File(paths.root, it).isDirectory }
            if (dirs.isNotEmpty()) extra["AEMU64_LIBPATH"] = dirs.joinToString(":") { "/$it" }
        }
        if (experimentalVibrationEnabled && argv.first() == "/vendor/bin/hw/vendor.qti.hardware.vibrator.service") {
            if (experimentalVibrationPrepared) spawnExperimentalVibration(svc.name, argv, props, extra)
            else log("vibration: experimental service unavailable, skipping")
        } else spawn(svc.name, guestRunner.cmdline(argv, props), guestRunner.env(extra))
    }

    /** a 64-bit ELF or a script */
    private fun runsNatively(f: File): Boolean = runCatching {
        f.inputStream().use { i ->
            val b = ByteArray(5)
            i.read(b) == 5 && ((b[0] == 0x7f.toByte() && b[4] == 2.toByte()) || b[0] == 0x23.toByte())
        }
    }.getOrDefault(false)

    private fun trackGuestProcess(name: String, process: Process) {
        synchronized(procs) {
            externalProcs.add(process)
            procs[name] = process
        }
    }

    private fun spawnExperimentalVibration(name: String, argv: List<String>, propsFile: File?, extra: Map<String, String>) = lifecycle.ifRunning {
        synchronized(vibrationLock) { if (vibrationChannel != null) return@ifRunning }
        val channel = try { HostVibrationChannel.prepare(ctx) } catch (failure: Throwable) {
            try { disableExperimentalVibration() } catch (cleanup: Throwable) { failure.addSuppressed(cleanup) }
            throw failure
        }
        synchronized(vibrationLock) { vibrationChannel = channel }
        try {
            val vibrationEnv = extra + mapOf(
                "AEMU64_VIB_PRELOAD" to "1",
                "RME_VIBRATION_SOCKET" to channel.socketName,
                "RME_VIBRATION_HOST_PID" to AProcess.myPid().toString(),
                "RME_VIBRATION_HOST_UID" to AProcess.myUid().toString(),
                "RME_VIBRATION_EPOCH" to channel.epoch.toString(),
            )
            spawn(name, guestRunner.cmdline(argv, propsFile, experimentalVibration = true), guestRunner.env(vibrationEnv), channel)
        } catch (failure: Throwable) {
            try { retireExperimentalVibration(channel) } catch (cleanup: Throwable) { failure.addSuppressed(cleanup) }
            throw failure
        }
    }

    private fun retireExperimentalVibration(channel: HostVibrationChannel) {
        try { channel.close() } finally {
            val owned = synchronized(vibrationLock) {
                if (vibrationChannel === channel) { vibrationChannel = null; true } else false
            }
            if (owned) disableExperimentalVibration()
        }
    }

    private fun disableExperimentalVibration() {
        if (!experimentalVibrationPrepared) return
        TargetVibrationDeclaration.prepare(paths.root, img.api, img.model, img.abi, engine, false)
        experimentalVibrationPrepared = false
    }

    /** Before any other guest services start, verify that the manager answers a bounded request. */
    private fun awaitDirectVibrationManager(runner: GuestRunner, manager: Process): Boolean {
        val deadline = SystemClock.uptimeMillis() + 8_000
        while (!stopping && manager.isAlive && SystemClock.uptimeMillis() < deadline) {
            val answer = try { runner.run(listOf("/system/bin/service", "list"), 2_000) }
                catch (interrupted: InterruptedException) { Thread.currentThread().interrupt(); throw interrupted }
                catch (_: Exception) { null }
            if (answer != null && answer.first == 0 && answer.second.trimStart().startsWith("Found ")) return true
            Thread.sleep(100)
        }
        return false
    }

    private fun startDirectVibration(binder: ManagedGuestProcess, control: HostVibrationControl, manager: Process) {
        // First two words were verified against a fixed target-Guest test; SYST follows.
        val header = byteArrayOf(0, 0, 0, 0x80.toByte(), -1, -1, -1, -1, 0x54, 0x53, 0x59, 0x53)
        val transport = NativeVibrationBinderTransport.connect(binder, paths.binderSock, header)
        var bridge: HostVibrationBinderBridge? = null
        try {
            bridge = HostVibrationBinderBridge(control, transport)
            val owned = bridge
            lifecycle.create { synchronized(directVibrationLock) { directVibrationBridge = owned } }
            check(transport.awaitRegistration(5_000) && manager.isAlive && binder.isAlive && !stopping) {
                "Vibration registration unavailable"
            }
        } catch (failure: Throwable) {
            try {
                if (bridge != null) bridge.close() else transport.close()
            } catch (cleanup: Throwable) { if (cleanup !== failure) failure.addSuppressed(cleanup) }
            synchronized(directVibrationLock) { if (directVibrationBridge === bridge) directVibrationBridge = null }
            throw failure
        }
    }

    private fun closeDirectVibration(): Exception? = try {
        val bridge = synchronized(directVibrationLock) { directVibrationBridge.also { directVibrationBridge = null } }
        bridge?.close()
        null
    } catch (failure: Exception) { failure }

    /** Never clear a cached declaration until both early processes have really exited. */
    private fun stopEarlyVibrationProcess(name: String) {
        val process = synchronized(procs) { procs[name] } ?: error("Missing early process $name")
        ourKills.add(process)
        if (process.isAlive) process.destroyForcibly()
        check(process.waitFor(5, java.util.concurrent.TimeUnit.SECONDS) && !process.isAlive) { "$name did not exit" }
        synchronized(procs) { if (procs[name] === process) procs.remove(name) }
    }

    private fun closeExperimentalVibration(): Exception? = try {
        val channel = synchronized(vibrationLock) { vibrationChannel }
        if (channel != null) retireExperimentalVibration(channel) else disableExperimentalVibration()
        null
    } catch (failure: Exception) { failure }

    private fun spawn(name: String, cmd: List<String>, env: Map<String, String>, vibration: HostVibrationChannel? = null) = lifecycle.ifRunning {
        val log = paths.log(name)
        val pb = ProcessBuilder(cmd).directory(paths.bin).redirectErrorStream(true)
        pb.environment().clear()
        pb.environment().putAll(if (env.isEmpty()) mapOf("PATH" to "/system/bin") else env)
        // 7.0+ zygote refuses to fork with pipes among its descriptors (it only reopens files and devices):
        // its stdio go straight to the log file and /dev/null, which the guest shim shows as /dev/null
        val direct = name == "zygote" && img.api >= 24
        if (direct) {
            runCatching { log.appendText("\n=== $name ${stamp()} ===\n") }
            pb.redirectInput(File("/dev/null")).redirectOutput(ProcessBuilder.Redirect.appendTo(log))
        }
        val processChannel = if (name == "aemu-radio") radioEvents else null
        val p = if (vibration != null) guestLauncher.startLimitedVibrationExperiment(pb, paths.root)
            else guestLauncher.start(pb, requireAuthenticated = processChannel != null)
        try {
        trackGuestProcess(name, p)
        vibration?.let { channel ->
            Thread({
                try { p.waitFor() } finally { retireExperimentalVibration(channel) }
            }, "vibration-exit").apply { isDaemon = true; start() }
            Thread({
                var attached = false
                try { attached = channel.attach(p) } finally {
                    if (!attached) {
                        log("vibration: event channel authentication failed")
                        runCatching { p.destroyForcibly() }
                    }
                }
            }, "vibration-attach").apply { isDaemon = true; start() }
        }
        processChannel?.let { channel ->
            Thread({
                try { waitForRadioProcess(p, channel) }
                catch (interrupted: InterruptedException) {
                    Thread.currentThread().interrupt()
                    log("radio: process watcher interrupted")
                } catch (failure: Exception) {
                    log("radio: process watcher cleanup failed: ${failure.javaClass.simpleName}")
                }
            }, "radio-event-exit").apply { isDaemon = true; start() }
            Thread({
                var attached = false
                try {
                    attached = channel.attach(p)
                    if (attached) log("radio: authenticated event channel ready")
                } finally {
                    if (!attached) {
                        log("radio: event channel authentication failed")
                        runCatching { p.destroyForcibly() }
                    }
                }
            }, "radio-event-attach").apply { isDaemon = true; start() }
        }
        log("· $name started")
        // init publishes every service's state; the framework waits on it (SystemService.waitForState: the WebView
        // zygote, the boot animation)
        props.put("init.svc.$name", "running")
        Thread({
            if (!direct) runCatching {
                OutputStreamWriter(FileOutputStream(log, true)).use { w ->
                    w.write("\n=== $name ${stamp()} ===\n")
                    // a looping service once wrote 455 MB in one run: past the cap the output is only drained
                    var n = 0L; var cut = false
                    p.inputStream.bufferedReader().forEachLine {
                        if (n < LogCap.SERVICE_RUN) { w.write(it); w.write("\n"); w.flush(); n += it.length + 1 }
                        else if (!cut) { cut = true; w.write("=== output cut at ${LogCap.SERVICE_RUN shr 20} MB ===\n"); w.flush() }
                    }
                }
            }
            val code = runCatching { p.waitFor() }.getOrDefault(-1)
            if (!p.isAlive) externalProcs.remove(p)
            if (!stopping) {
                log("· $name exited, code $code")
                // (a process the engine stopped itself - boot animation, planned restart - is not a crash)
                if (code != 0 && !ourKills.remove(p)) LogCap.crash(paths, name, code, if (name == "zygote" && code == 137) systemServerDeath() else null)
            }
            // a restarted service is already running again under this name
            synchronized(procs) { if (procs[name].let { it == null || it === p }) props.put("init.svc.$name", "stopped") }
        }, "log-$name").apply { isDaemon = true; start() }
        } catch (failure: Throwable) {
            if (vibration != null) runCatching { p.destroyForcibly() }
            throw failure
        }
    }

    private fun alive(): List<String> = synchronized(procs) { procs.filterValues { it.isAlive }.keys.toList() }

    private fun waitFor(what: String, ms: Long, cond: () -> Boolean): Boolean {
        val until = SystemClock.uptimeMillis() + ms
        while (SystemClock.uptimeMillis() < until && !stopping) {
            if (cond()) return true
            Thread.sleep(100)
        }
        val ok = cond()
        if (!ok) log("⚠ timed out waiting for: $what")
        return ok
    }

    // ------------------------------------------------------------------ присмотр

    /** Guest asked to reboot / power off ("reboot", "reboot,recovery", "shutdown"); the UI restarts or stops the VM. */
    @Volatile var onPower: ((reboot: Boolean, reason: String) -> Unit)? = null
    private val powerHandled = java.util.concurrent.atomic.AtomicBoolean(false)

    private fun powerRequest(v: String) {
        if (v.isEmpty() || !powerHandled.compareAndSet(false, true)) return
        val reboot = v.startsWith("reboot")
        val reason = v.substringAfter(',', "")
        log("guest requested ${if (reboot) "reboot" else "power off"}${if (reason.isNotEmpty()) " ($reason)" else ""}")
        runCatching { PowerRequestHistory(paths.bin).record(v) }
            .onFailure { error -> log("cannot save guest power request: ${error.message}") }
        if (reason == "recovery" && !RecoveryImage.installed(paths)) log("no recovery installed, booting the system again")
        onPower?.invoke(reboot, reason)
    }

    /**
     * 12+: the generator of the linker's namespaces takes the active modules from /apex/apex-info-list.xml, which
     * apexd writes when it mounts them; here they are plain directories, so the list is written from their manifests.
     */
    private fun apexInfoList() {
        val apex = File(paths.root, "apex")
        val mods = apex.listFiles()?.filter { it.isDirectory && '@' !in it.name && File(it, "apex_manifest.pb").isFile }?.sortedBy { it.name } ?: return
        if (mods.isEmpty()) return
        val pre = File(paths.root, "system/apex").list() ?: emptyArray()
        val sb = StringBuilder("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<apex-info-list>\n")
        for (m in mods) {
            // apex_manifest.pb: field 1 the name, field 2 the version (a varint)
            val b = File(m, "apex_manifest.pb").readBytes()
            var i = 0; var version = 1L
            fun varint(): Long { var v = 0L; var sh = 0; while (i < b.size) { val c = b[i++].toInt(); v = v or ((c and 0x7f).toLong() shl sh); sh += 7; if (c and 0x80 == 0) break }; return v }
            while (i < b.size) {
                val tag = varint().toInt()
                when (tag and 7) { 0 -> { val v = varint(); if (tag shr 3 == 2) version = v }; 2 -> i += varint().toInt(); 1 -> i += 8; 5 -> i += 4; else -> i = b.size }
            }
            val file = pre.firstOrNull { it == m.name || it.substringBeforeLast('.') == m.name || it.startsWith(m.name + ".") } ?: m.name
            sb.append("  <apex-info moduleName=\"${m.name}\" modulePath=\"/system/apex/$file\" preinstalledModulePath=\"/system/apex/$file\" versionCode=\"$version\" versionName=\"\" isFactory=\"true\" isActive=\"true\" />\n")
        }
        sb.append("</apex-info-list>\n")
        File(apex, "apex-info-list.xml").writeText(sb.toString())
    }

    private fun clientComposition() {
        runCatching { guestShell("service call SurfaceFlinger 1008 i32 1", 15_000) }
    }

    private fun onProp(k: String, v: String) {
        if (k == "sys.powerctl") { powerRequest(v); return }
        // SurfaceFlinger has just come up (it resets this before starting the boot animation)
        if (k == "service.bootanim.exit" && v == "0" && engine == Engine.N64 && kernel.clientComposition)
            lifecycle.launch("vm-composition") { clientComposition() }
        if (k == "service.bootanim.exit" && v == "1" && screenUpAt == 0L) screenUpAt = System.currentTimeMillis()
        // The animation notices the request late or never (open32, and the N64 kernels too: 7.1 on kernel 25), and
        // 7.x WindowManager keeps the screen off and input frozen until init.svc.bootanim is "stopped" (BOOT_TIMEOUT,
        // 30 s): the "frozen" half minute at the end of boot. It gets 1.5 s.
        if (k == "service.bootanim.exit" && v == "1") lifecycle.launch("vm-stop-bootanim") {
            Thread.sleep(1500)
            killOurs(synchronized(procs) { procs["bootanim"]?.takeIf { it.isAlive }?.let { procs.remove("bootanim") } })
        }
        if ((k == "sys.boot_completed" || k == "dev.bootcomplete") && v == "1") booted()
    }

    private fun booted() = lifecycle.ifRunning {
        if (bootDoneAt == 0L) {
            bootDoneAt = System.currentTimeMillis()
            everBooted = true
            log("★ guest reported boot completed in ${(bootDoneAt - bootAt) / 1000} s")
            setState(State.RUNNING)
            lifecycle.launch("vm-after-boot") { afterBoot() }
        }
    }

    /**
     * The modules ApexStub reports (<root>/apex/.aemu-apexes): name, package file, preinstalled path, version. The
     * package manager parses the package file itself (its AndroidManifest.xml), so it is the one the importer set
     * aside when it unpacked the module; the preinstalled path tells it the partition.
     */
    private fun apexStandInList() = runCatching {
        val apex = File(paths.root, "apex")
        val out = File(apex, ".aemu-apexes")
        val parked = File(paths.root, "system/.aemu-parked").listFiles()?.filter { it.name.endsWith(".apex") || it.name.endsWith(".capex") }?.sortedBy { it.name } ?: emptyList()
        if (out.isFile && out.lastModified() >= (parked.maxOfOrNull { it.lastModified() } ?: 0L) && out.length() > 0) return@runCatching
        fun fields(b: ByteArray): Pair<String?, Long> {
            var i = 0; var name: String? = null; var version = 1L
            fun varint(): Long { var v = 0L; var sh = 0; while (i < b.size) { val c = b[i++].toInt(); v = v or ((c and 0x7f).toLong() shl sh); sh += 7; if (c and 0x80 == 0) break }; return v }
            while (i < b.size) {
                val tag = varint().toInt()
                when (tag and 7) {
                    0 -> { val v = varint(); if (tag shr 3 == 2) version = v }
                    2 -> { val n = varint().toInt(); if (tag shr 3 == 1 && i + n <= b.size) name = String(b, i, n); i += n }
                    1 -> i += 8; 5 -> i += 4; else -> i = b.size
                }
            }
            return name to version
        }
        val sb = StringBuilder(); val seen = HashSet<String>()
        for (f in parked) runCatching {
            val (name, version) = java.util.zip.ZipFile(f).use { z -> fields(z.getInputStream(z.getEntry("apex_manifest.pb")).use { it.readBytes() }) }
            if (name == null || !seen.add(name) || !File(apex, "$name/apex_manifest.pb").isFile) return@runCatching
            // "system#apex#x.apex" is where the package was: /system/apex/x.apex
            val was = "/" + f.name.replace('#', '/')
            sb.append("$name\t/system/.aemu-parked/${f.name}\t$was\t$version\n")
        }
        out.writeText(sb.toString())
        log("apex stand-in: ${seen.size} modules")
    }.onFailure { log("apex stand-in: ${it.message}") }

    private fun onCtl(start: Boolean, svc: String) = lifecycle.ifRunning { onCtlWhileRunning(start, svc) }

    private fun onCtlWhileRunning(start: Boolean, svc: String) {
        if (svc.startsWith("aidl/")) {
            // a lazy AIDL service (artd of 14+, a skin's daemon): started when servicemanager is asked for it, and
            // a few times at most, as the question comes every second for as long as the client waits
            if (!kernel.lazyAidlServices) return
            if (kernel.apexStandIn && svc == "aidl/apexservice") return
            val def = InitPlan.lazyAidl(svc.removePrefix("aidl/"), paths.root) ?: return
            synchronized(procs) {
                if (procs[def.name]?.isAlive == true) return
                val n = lazyStarts[def.name] ?: 0
                if (n >= 3) return
                lazyStarts[def.name] = n + 1
            }
            log("lazy service ${def.name} asked for ($svc)")
            lifecycle.launch("vm-start-service") { runCatching { startService(def) } }
            return
        }
        if (wifi?.onCtl(start, svc) == true) return
        val plan = servicePlan()
        if (svc == "bootanim" || svc == "bootanimation") {
            if (!start) {
                // 4.x sets service.bootanim.exit first and the animation quits on its own, closing its audio.
                // Killing it outright left Samsung's boot sound track half-open: mediaserver hung and every
                // app creating a sound (the phone process, ToneGenerator) got an ANR.
                val p = synchronized(procs) { procs["bootanim"] } ?: return
                lifecycle.launch("vm-stop-service") {
                    runCatching { if (!p.waitFor(5, java.util.concurrent.TimeUnit.SECONDS)) killOurs(p) }
                }
                return
            }
            // below 4.1 (2.3, MIUI ICS) the animation does not quit by itself: killed, it leaves the GL bridge on its last frame
            if (img.api < 16 || bootDoneAt != 0L || synchronized(procs) { procs.containsKey("bootanim") }) return
            InitPlan.optional("bootanim", img, paths.root)?.let { def ->
                lifecycle.launch("vm-start-service") { runCatching { startService(def) } }
                log("boot animation started")
            }
            return
        }
        val def = plan.firstOrNull { it.name == svc } ?: InitPlan.optional(svc, img, paths.root)
        if (def == null) { log("ctl.${if (start) "start" else "stop"} $svc: no such service"); return }
        killOurs(synchronized(procs) { procs.remove(svc) })
        if (start) lifecycle.launch("vm-start-service") { runCatching { startService(def) } }
    }

    private fun afterBoot() {
        val r = guestRunner
        if (skipWizardNow()) return   // the system comes up again and gets here once more
        lifecycle.launch("vm-ringtone-index") {
            try {
                val result = TargetRingtoneIndex.ensureWithRetry(img.api, img.model, img.abi, engine, { wait ->
                    var remaining = wait
                    while (remaining > 0 && !stopping) {
                        val step = minOf(remaining, 200L)
                        Thread.sleep(step)
                        remaining -= step
                    }
                    !stopping
                }, { message -> log("ringtone: $message") }) { command, timeout ->
                    lifecycle.run { r.run(listOf("/system/bin/sh", "-c", shellPrefix + command), timeout) }
                }
                when (result) {
                    TargetRingtoneIndex.Result.SCANNED -> {
                        log("ringtone: system audio indexed")
                        initializeFirstRingtones(r)
                    }
                    TargetRingtoneIndex.Result.FAILED -> log("ringtone: system audio indexing failed")
                    TargetRingtoneIndex.Result.CANCELLED -> log("ringtone: system audio indexing cancelled")
                    TargetRingtoneIndex.Result.NOT_APPLICABLE -> Unit
                }
            } catch (e: Exception) {
                log("ringtone: system audio indexing failed: ${e.javaClass.simpleName}")
            }
        }
        // open32: right after the boot completes a dozen Google apps wake up at once, each 100-250 MB under the
        // translator; the host runs out of memory and swaps (kswapd at 100%), the screen stands still for ~15 s and
        // LMK kills and restarts them in a loop. Two background processes at most (AMS setProcessLimit, 7.x code 51).
        // 2.x-4.x: vendor launchers keep providers in processes of their own (TouchWiz BadgeProvider); with two slots AMS
        // exits that "empty" process and kills TwLauncher with it ("provider in dying process", forum: TwLauncher crashes)
        if (open32) runCatching { guestShell("service call activity 51 i32 ${if (img.api < 21) 6 else 2}", 15_000) }
        if (open32 && img.api >= 24) lifecycle.launch("vm-compile") { compileVisibleApps() }   // "cmd package compile" is 7.0+
        // (again: something in the first seconds after the boot completes turns it back off, and it is not known what;
        // a call made half a minute later stays)
        if (engine == Engine.N64 && kernel.clientComposition) lifecycle.launch("vm-composition-delay") {
            for (wait in longArrayOf(0, 10_000, 20_000)) {
                var remaining = wait
                while (remaining > 0 && !stopping) {
                    val step = minOf(remaining, 200L)
                    Thread.sleep(step); remaining -= step
                }
                if (state != State.RUNNING || stopping) return@launch
                clientComposition()
            }
        }
        if (stopping) return
        r.run(listOf("/system/bin/sh", "-c", "export PATH=/system/bin:/system/xbin:\$PATH; svc power stayon true"), 60_000)
        if (stopping) return
        if (engine == Engine.N64 && kernel.webViewSingleProcess)
            runCatching { guestShell("cmd webviewupdate disable-multiprocess", 30_000) }
        TreeFixer(ctx, paths, img, ::log).noScreenSleep()
        disableBrokenComponents()
        if (stopping) return
        if (img.api >= 24) runCatching { enableHome() }.onFailure { log("home screen check failed: $it") }
        val img2 =img.copy(lastBootMs = bootDoneAt - bootAt, bootCount = img.bootCount + 1)
        ImageStore.save(ctx, img2)
    }

    private fun initializeFirstRingtones(runner: GuestRunner) {
        if (img.api != 36 || img.model != "NX809J" || img.abi != "arm64-v8a" || engine != Engine.N64) return
        try {
            FirstRingtoneInitStore.open(paths.dir, img.id).use { store ->
                val result = FirstRingtoneScan.execute(
                    store, img.api, img.model, img.abi, engine, systemAudioIndexed = true,
                    pause = { wait ->
                        var remaining = wait
                        while (remaining > 0 && !stopping) {
                            val step = minOf(remaining, 200L)
                            Thread.sleep(step)
                            remaining -= step
                        }
                        !stopping
                    },
                ) { command, timeout ->
                    if (command == "content call --uri content://media --method scan_volume --arg external_primary")
                        log("ringtone: starting default volume scan")
                    when (val completed = lifecycle.run {
                        runner.runBounded(listOf("/system/bin/sh", "-c", shellPrefix + command), timeout)
                    }) {
                        is BoundedGuestCommand.Result.Completed -> TargetRingtoneDefaults.CommandResult.Completed(
                            completed.exitCode, completed.output, outputComplete = true,
                        )
                        is BoundedGuestCommand.Result.Uncertain -> TargetRingtoneDefaults.CommandResult.TimedOut
                    }
                }
                when (result) {
                    FirstRingtoneScan.Result.Skipped -> Unit
                    FirstRingtoneScan.Result.NotReady -> log("ringtone: first initialization not ready")
                    FirstRingtoneScan.Result.ObservedMatchingDefaults -> log("ringtone: first defaults verified")
                    FirstRingtoneScan.Result.ConfiguredWithUserChoices -> log("ringtone: first settings verified; non-default or silent selections retained")
                    FirstRingtoneScan.Result.Uncertain -> log("ringtone: first initialization unverified")
                }
            }
        } catch (e: Exception) {
            log("ringtone: first initialization failed: ${e.javaClass.simpleName}")
        }
    }

    /**
     * The first boot ends in the vendor's setup wizard: the package list it has to be switched off in exists only
     * now, and the running system keeps its own copy of the settings. Several wizards cannot be passed here at all
     * (Sony's 4.0 one shows an empty screen, Samsung's waits for the network time), so the system is restarted once,
     * with the wizard disabled and the device marked as set up while it is down.
     */
    private fun skipWizardNow(): Boolean {
        if (wizardSkipped || zygoteRestarts > 0 || img.bootCount > 0) return false
        // MIUI ships its own wizard under the AOSP provisioner's name, and it does not finish by itself
        val wizards = TreeFixer.FIRST_RUN_WIZARDS + (if (File(paths.root, "system/app/miui/miui.apk").isFile) listOf("com.android.provision") else emptyList())
        val pkgFile = File(paths.root, "data/system/packages.xml")
        // 12+ keeps its package lists in binary XML (ABX) - and a text one written by request (persist.sys.binary_xml)
        // is not read back by 14 ("illegal attribute name: :strings"). There the running system is asked instead.
        if (runCatching { pkgFile.inputStream().use { i -> ByteArray(3).also { i.read(it) } }.contentEquals("ABX".toByteArray()) }.getOrDefault(false)) {
            val listed = runCatching { guestShell("pm list packages", 60_000) }.getOrDefault("")
            val wizard = wizards.firstOrNull { listed.lines().any { l -> l.trim() == "package:$it" } } ?: return false
            wizardSkipped = true
            plannedRestart = true
            log("first boot: setup wizard ($wizard) switched off in the running system, restarting it")
            runCatching { guestShell("settings put global device_provisioned 1; settings put secure user_setup_complete 1; pm disable-user --user 0 $wizard", 60_000) }
            killZygoteOurs()
            return true
        }
        val pkgs = runCatching { pkgFile.readText() }.getOrNull() ?: return false
        val restr = runCatching { File(paths.root, "data/system/users/0/package-restrictions.xml").readText() }.getOrDefault("")
        val wizard = wizards.firstOrNull {
            pkgs.contains("<package name=\"$it\"") && !Regex("<pkg name=\"${Regex.escape(it)}\"[^>]*enabled=\"2\"").containsMatchIn(restr)
        } ?: return false
        wizardSkipped = true
        plannedRestart = true
        log("first boot: restarting the system past the setup wizard ($wizard)")
        killZygoteOurs()
        return true
    }

    /**
     * The home screen of a firmware can ship switched off and be switched on by its setup wizard or by an app that
     * runs during it (the Google app's launcher activity on Nexus 8.0, turned on for the GoogleHome stub). With the
     * wizard skipped the only home left is Settings' FallbackHome, a black screen. Then every home activity the
     * package manager knows of but does not resolve, in a package that has an entry in the app list, is switched on.
     */
    private fun enableHome() {
        val resolved = guestShell("cmd package query-activities --brief -a android.intent.action.MAIN -c android.intent.category.HOME", 30_000)
        val homes = Regex("""\S+/\S+""").findAll(resolved).map { it.value }.toSet()
        if (homes.any { !it.endsWith("FallbackHome") }) return
        val table = guestShell("dumpsys package -f r", 90_000).lines()
        val off = LinkedHashSet<String>()
        for (i in table.indices) {
            if (!table[i].contains("\"android.intent.category.HOME\"")) continue
            // the component line is the one above the filter's action/category lines
            val comp = (i downTo maxOf(0, i - 8)).firstNotNullOfOrNull { j -> Regex("""\s[0-9a-f]+ (\S+/\S+) filter""").find(table[j])?.groupValues?.get(1) } ?: continue
            if (comp !in homes) off += comp
        }
        // (not the system's own fallbacks, and not a setup or recovery flow: a launcher's package shows in the app list)
        val own = homes.map { it.substringBefore('/') }.toSet() + "android"
        val listed = Regex("""(\S+)/\S+""").findAll(guestShell("cmd package query-activities --brief -a android.intent.action.MAIN -c android.intent.category.LAUNCHER", 30_000))
            .map { it.groupValues[1] }.toSet()
        val c = off.firstOrNull { it.substringBefore('/') !in own && it.substringBefore('/') in listed } ?: return
        log("home screen $c was switched off, switching it on")
        guestShell("pm enable $c; am start -a android.intent.action.MAIN -c android.intent.category.HOME", 30_000)
    }

    /**
     * 2.3–4.1 Google network location (in Play services and in Maps) calls TelephonyRegistry.listen with notifyNow at
     * start: the reply of that call is lost between the nested oneway callback and the reply, and the service dies with
     * "Unknown exception code" and a "has stopped" dialog on every boot. Nothing here needs network location.
     */
    private fun disableBrokenComponents() {
        if (img.api !in 9..16) return
        val marker = File(paths.root, "data/.aemu-components-disabled2")
        if (marker.isFile) return
        val list = listOf(
            "com.google.android.location/com.google.android.location.NetworkLocationService",
            "com.google.android.location/com.google.android.location.internal.server.NetworkLocationService",
            "com.google.android.apps.maps/com.google.android.location.internal.server.NetworkLocationService",
            "com.google.android.apps.maps/com.google.android.location.NetworkLocationService",
        )
        // PATH of 2.3–4.x starts with /sbin: the pm script's "app_process" is then looked up there and qemu gives up
        for (c in list) { if (stopping) return; runCatching { guestRunner.run(listOf("/system/bin/sh", "-c", "export PATH=/system/bin:/system/xbin:\$PATH; pm disable $c"), 120_000) } }
        runCatching { marker.writeText("1") }
    }

    /** qemu maps a guest path into the tree only if the file exists there, so the shim's request file is pre-made */
    private val power: File get() = File(paths.root, "dev/aemu_power")

    private fun watchdog() {
        runCatching { power.writeText(""); power.setWritable(true, false) }
        lifecycle.launch("vm-watchdog") {
            var restarts = HashMap<String, Int>()
            while (!stopping) {
                Thread.sleep(3000)
                if (stopping) break
                if (power.length() > 0) { val v = runCatching { power.readText().trim() }.getOrDefault("reboot"); power.writeText(""); powerRequest(v) }
                if (img.api >= 14) runCatching { dns.ensure() }
                val dead = synchronized(procs) { procs.filter { !it.value.isAlive }.keys.toList() }
                for (name in dead) {
                    if (stopping) break
                    val p = synchronized(procs) { procs[name] } ?: continue
                    val code = runCatching { p.exitValue() }.getOrDefault(-1)
                    if (name == "zygote" || name == "zygote_secondary") {
                        if (state != State.FAILED && bootDoneAt == 0L && !(everBooted && zygoteRestarts < 6)) {
                            failure = "$name exited (code $code)" + if (code == 137) " — killed: the phone ran out of memory, or system_server died and took zygote with it (see the guest log)" else ""
                            // (not "No original dex files found": our own `input`/`wm` helpers log that for unrelated jars)
                            if (code == 137 && guestLogHas("ClassNotFoundException: com.android.server.SystemServer"))
                                failure = "system_server cannot start: the precompiled code of the framework (system/framework/oat) is missing from this container — import the firmware again"
                            log("✖ $failure"); setState(State.FAILED)
                        } else if (plannedRestart) {
                            plannedRestart = false
                            killZygoteChildren()
                            TreeFixer(ctx, paths, img, ::log).noScreenSleep()
                            restartZygote()
                        } else if (bootDoneAt > 0 || everBooted) {
                            zygoteRestarts++
                            val now = System.currentTimeMillis()
                            zygoteCrashes.addLast(now)
                            while (zygoteCrashes.isNotEmpty() && zygoteCrashes.first() < now - 600_000) zygoteCrashes.removeFirst()
                            val recentCrashesIn1Min = zygoteCrashes.count { it >= now - 60_000 }
                            if (recentCrashesIn1Min >= 5 || zygoteCrashes.size > 6) {
                                failure = "system keeps crashing: $name exited $recentCrashesIn1Min times in 1 min (${zygoteCrashes.size} times in 10 min, last code $code)"
                                log("✖ $failure"); setState(State.FAILED)
                            } else {
                                log("✖ $name crashed (code $code), restarting system ($zygoteRestarts)")
                                restartZygote()
                            }
                        }
                        synchronized(procs) { procs.remove(name) }
                        continue
                    }
                    val n = restarts.getOrDefault(name, 0)
                    val svc = img.services.firstOrNull { it.name == name }
                        ?: if (name.contains("-hal-") && n < 3) servicePlan().firstOrNull { it.name == name } else null
                    // init restarts surfaceflinger and then zygote ("onrestart restart zygote"); without that a
                    // crashed compositor leaves the last frame on screen forever
                    if (name == "surfaceflinger" && svc != null && bootDoneAt > 0 && n < 12) {
                        restarts[name] = n + 1
                        log("✖ surfaceflinger crashed (code $code), restarting it and the system (${n + 1}/12)")
                        synchronized(procs) { procs.remove(name) }
                        runCatching { startService(svc) }
                        killZygoteOurs()
                        continue
                    }
                    // родной audio_policy производителя может падать с нашим HAL — подменяем на AOSP
                    if (name == "mediaserver" && (code == 139 || code == 134)) swapAudioPolicy(crashed = n >= 3)
                    // (init restarts every service that is not oneshot; a HAL that leaves when its only client, system_server, is
                    // restarted would otherwise keep the new system_server waiting for it forever)
                    val hal = name.contains("-hal-") && n < 3
                    if (svc != null && (svc.restart || name in ALWAYS_RESTART || hal) && code != 137 && code != 143 && n < 12) {
                        restarts[name] = n + 1
                        log("service $name crashed (code $code), restarting (${n + 1}/12)")
                        synchronized(procs) { procs.remove(name) }
                        runCatching { startService(svc) }
                    } else synchronized(procs) { procs.remove(name) }
                }
            }
        }
    }

    /**
     * Ставит политику AOSP 4.3 за обёрткой: родная сохраняется, лишние потоки производителя
     * (Samsung: 0..14 против 0..9 в AOSP) обёртка сводит к MUSIC, чтобы AOSP-код не писал за массив.
     */
    /** libaudioflinger carries AudioPolicyManagerBase (the class the emulator branch of 2.2-2.3 creates) */
    private val policyBase by lazy {
        runCatching {
            val b = File(paths.root, "system/lib/libaudioflinger.so").readBytes()
            val n = "AudioPolicyManagerBase".toByteArray()
            (0..b.size - n.size).any { i -> b[i] == n[0] && (n.indices).all { b[i + it] == n[it] } }
        }.getOrDefault(true)
    }
    private fun hasPolicyBase(): Boolean = policyBase

    private fun swapAudioPolicy(crashed: Boolean = false) {
        if (img.api < 16) return
        val f = File(paths.root, "system/lib/hw/audio_policy.default.so")
        val aosp = File(paths.root, "system/lib/libaemu_apaosp.so")
        val parked = File(paths.root, "system/.aemu-parked/system#lib#hw#audio_policy.default.so")
        // MediaTek: AudioPolicyService переделан (другие ops и слоты), политика AOSP в нём падает.
        // Родная политика MTK падала только из-за проверки DRVB, которую теперь снимает TreeFixer
        if (TreeFixer.isMtkAudio(paths.root)) {
            if (parked.isFile) runCatching {
                parked.copyTo(f, overwrite = true); parked.delete(); aosp.delete()
                log("audio: MediaTek, restored stock audio_policy")
            }
            return
        }
        // the AOSP policy itself crashes in some vendors' audio services (Samsung 4.4: its first output is not found
        // right after it is added): after three more crashes in a row it is taken out again for good, and mediaserver
        // runs as it did before the swap
        val bad = File(paths.root, "system/.aemu-parked/audio_policy.aosp-bad")
        if (bad.isFile) return
        fun assetSize(n: String) = runCatching { EngineAssets.size(ctx, "engines/kk/$n") }.getOrDefault(-1L)
        if (aosp.length() == assetSize("audio_policy.default.so") && f.length() == assetSize("audio_policy.wrap.so")) {
            if (!crashed) return
            runCatching {
                bad.parentFile?.mkdirs(); bad.writeText("")
                aosp.delete()
                if (parked.isFile) { parked.copyTo(f, overwrite = true); parked.delete() } else f.delete()
                log("audio: the AOSP audio_policy crashes here too, removed it")
            }
            return
        }
        runCatching {
            parked.parentFile?.mkdirs()
            if (!parked.isFile && f.isFile) f.copyTo(parked, overwrite = true)
            EngineAssets.open(ctx, "engines/kk/audio_policy.default.so").use { i -> aosp.outputStream().use { o -> i.copyTo(o) } }
            EngineAssets.open(ctx, "engines/kk/audio_policy.wrap.so").use { i -> f.outputStream().use { o -> i.copyTo(o) } }
            log("audio: firmware audio_policy crashes, installed AOSP one (stock kept)")
        }
    }

    @Volatile private var plannedRestart = false

    /** processes the engine itself stopped: their exits are not crashes (run/crashes.log is the crash statistics) */
    private val ourKills: MutableSet<Process> = java.util.Collections.synchronizedSet(java.util.Collections.newSetFromMap(java.util.IdentityHashMap()))

    private fun killOurs(p: Process?) { if (p != null) { ourKills.add(p); runCatching { p.destroyForcibly() } } }

    private fun killZygoteOurs() {
        killOurs(synchronized(procs) { procs["zygote"] })
    }

    /**
     * zygote kills itself (SIGKILL, exit 137) when system_server dies (SigChldHandler in com_android_internal_os_Zygote.cpp,
     * dalvik_system_Zygote.c before 5.0). The reason is in the guest log: the uncaught exception (RuntimeInit's
     * "FATAL EXCEPTION IN SYSTEM PROCESS"), the watchdog ("WATCHDOG KILLING SYSTEM PROCESS") or a native crash.
     */
    private fun systemServerDeath(): String? = runCatching {
        val markers = listOf("FATAL EXCEPTION IN SYSTEM PROCESS", "WATCHDOG KILLING SYSTEM PROCESS", ">>> system_server <<<")
        var best: Pair<Long, String>? = null
        for (buf in listOf("main", "system", "crash")) {
            val f = File(paths.root, "dev/log/$buf")
            if (!f.isFile) continue
            val len = f.length(); val n = minOf(len, 2_000_000L).toInt()
            val b = ByteArray(n)
            java.io.RandomAccessFile(f, "r").use { r -> r.seek(len - n); r.readFully(b) }
            val t = String(b, Charsets.ISO_8859_1)
            for (m in markers) {
                val i = t.lastIndexOf(m); if (i < 0) continue
                // the ring is raw logger entries: keep the printable text after the marker
                val tail = t.substring(i, minOf(t.length, i + 600)).map { if (it in ' '..'~') it else ' ' }.joinToString("")
                    .replace(Regex(" {2,}"), " ").take(240)
                if (best == null || f.lastModified() > best!!.first) best = f.lastModified() to tail
            }
        }
        best?.second
    }.getOrNull()
    private var wizardSkipped = false

    private fun restartZygote() {
        val z = img.services.firstOrNull { it.name == "zygote" } ?: return
        bootDoneAt = 0
        setState(State.BOOTING)
        paths.socket("zygote").delete()
        paths.socket("zygote_secondary").delete()
        // на настоящем ядре дети зиготы гибнут вместе с system_server; здесь они остаются жить и
        // держат ссылки на мёртвые службы (телефония отказывает новому system_server в правах)
        val n = killZygoteChildren()
        if (n > 0) log("killed apps of previous zygote: $n")
        // servicemanager can leave together with system_server (Redmi 4 MIUI 9: exit 0); init restarts it and then,
        // by its "onrestart" lines, every daemon that registered there - else the new system_server finds no services
        val sm = img.services.firstOrNull { it.name == "servicemanager" }
        if (sm != null && synchronized(procs) { procs["servicemanager"] }?.isAlive != true) {
            synchronized(procs) { procs.remove("servicemanager") }
            runCatching { startService(sm) }
            Thread.sleep(300)
            for (name in listOf("surfaceflinger", "mediaserver", "drmserver", "keystore", "gatekeeperd")) {
                val s = img.services.firstOrNull { it.name == name } ?: continue
                synchronized(procs) { procs.remove(name) }?.let { killOurs(it); runCatching { it.waitFor() } }
                runCatching { startService(s) }
            }
            log("servicemanager was gone: restarted it and the daemons registered there")
        }
        // healthd keeps the battery listener of the dead system_server and stops answering the new one:
        // BatteryService.onStart then blocks in registerListener forever
        img.services.firstOrNull { it.name == "healthd" }?.let { h ->
            killOurs(synchronized(procs) { procs.remove("healthd") })
            runCatching { startService(h) }
        }
        runCatching { startService(z) }
        img.services.firstOrNull { it.name == "zygote_secondary" }?.let { z2 ->
            killOurs(synchronized(procs) { procs.remove("zygote_secondary") })
            runCatching { startService(z2) }
        }
    }

    private fun guestLogHas(text: String): Boolean = runCatching {
        val f = File(paths.root, "dev/log/main")
        val len = f.length()
        java.io.RandomAccessFile(f, "r").use { r ->
            val n = minOf(len, 400_000L).toInt(); r.seek(len - n)
            val b = ByteArray(n); r.readFully(b)
            String(b, Charsets.ISO_8859_1).contains(text)
        }
    }.getOrDefault(false)

    private fun killZygoteChildren(): Int {
        val marker = "/images/${img.id}/"
        var n = 0
        File("/proc").listFiles()?.forEach { d ->
            val pid = d.name.toIntOrNull() ?: return@forEach
            val cmd = runCatching { String(File(d, "cmdline").readBytes(), Charsets.ISO_8859_1) }.getOrNull() ?: return@forEach
            // app_process с aemu-stubs.jar — наша заглушка bluetooth_manager, не приложение зиготы
            if (cmd.contains(marker) && cmd.contains("/system/bin/app_process") && !cmd.contains("aemu-stubs.jar")) {
                runCatching { AProcess.sendSignal(pid, 9) }; n++
            } else if (orphanOfThisImage(d)) {
                // 7.0+: a zygote child rewrites its whole command line ("system_server"), so the path is gone from it;
                // what is left of a dead zygote is known by the root in its environment and by having no parent
                runCatching { AProcess.sendSignal(pid, 9) }; n++
            }
        }
        return n
    }

    /** a guest program of this image whose parent has exited (init of the host adopted it) */
    private fun orphanOfThisImage(proc: File): Boolean {
        val status = runCatching { File(proc, "status").readLines() }.getOrNull() ?: return false
        if (status.none { it.startsWith("PPid:") && it.substringAfter(':').trim() == "1" }) return false
        val env = runCatching { String(File(proc, "environ").readBytes(), Charsets.ISO_8859_1) }.getOrNull() ?: return false
        return env.split(0.toChar()).any { it == "AEMU64_ROOT=${paths.root.absolutePath}" || it == "AEMU64_ROOT=${paths.root.canonicalPath}" }
    }

    // ------------------------------------------------------------------ остановка

    fun stop() {
        var radioCleanupFailure: Exception? = null
        var vibrationCleanupFailure: Exception? = null
        lifecycle.stop(onRequest = {
            stopping = true
            setState(State.STOPPING)
            closeRadioChannelForStop(radioEvents) { radioEvents = null }?.let { failure ->
                radioCleanupFailure = failure
                log("radio: event channel cleanup failed: ${failure.javaClass.simpleName}")
            }
            activeRadioBackend = TargetRadioLaunch.Backend.DISABLED
            closeDirectVibration()?.let { failure ->
                vibrationCleanupFailure = failure
                log("vibration: host bridge cleanup failed: ${failure.javaClass.simpleName}")
            }
            directVibrationBlockQti = false
            closeExperimentalVibration()?.let { failure ->
                vibrationCleanupFailure = failure
                log("vibration: cleanup failed: ${failure.javaClass.simpleName}")
            }
            adb.requestStop()
            externalProcs.forEach { runCatching { it.destroyForcibly() } }
        }) {
            adb.stop()
            if (admissionState.admitted) {
                log("stopping system")
                runCatching { guestRunner.run(listOf("/system/bin/sync"), 5_000, start = ::startCleanupSync) }
                killAll()
                props.stop(); props.stopAbstract();input.stop(); frames.stop(); ril.stop(); wifi?.stop(); vold.stop(); if (audioDelegate.isInitialized()) audio.stop(); net.stop(); dns.stop(); events.stop(); logd.stop(); lights.stop(); sensors.stop(); mic.stop(); camera.stop(); venc.stop(); overlays.stop(); GlHost.stop()
                extraStubs.forEach { it.stop() }; extraStubs.clear()
                lmk?.stop(); lmk = null
                Keeper.release(ctx)
            }
            radioCleanupFailure?.let { throw it }
            vibrationCleanupFailure?.let { throw it }
            setState(State.STOPPED)
        }
    }

    /** The stop-time sync bypasses the closed start gate, but must still be reaped by killAll. */
    internal fun startCleanupSync(builder: ProcessBuilder): Process = guestLauncher.start(builder).also { externalProcs.add(it) }

    private fun killAll() {
        val named = synchronized(procs) { procs.toMap() }
        val list = (named.values.toList() + externalProcs.toList()).distinct()
        list.forEach { it.destroyForcibly() }
        val deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(5)
        list.forEach { process ->
            val remaining = deadline - System.nanoTime()
            val exited = if (remaining > 0) process.waitFor(remaining, java.util.concurrent.TimeUnit.NANOSECONDS) else !process.isAlive
            if (!exited) {
                val alive = list.filter { it.isAlive }.joinToString { p ->
                    val name = named.entries.firstOrNull { it.value === p }?.key ?: "external"
                    val pid = runCatching { pidOf(p) }.getOrNull()
                    "$name (pid ${pid ?: "unknown"})"
                }
                log("stop: processes still alive after termination: $alive")
                error("Guest process did not exit after termination")
            }
        }
        externalProcs.clear()
        synchronized(procs) { procs.clear() }
        killLeftovers()
    }

    /** Добивает процессы гостя, оставшиеся от прошлых запусков (qemu, binderd, dhdrun). */
    fun killLeftovers(): Int {
        val mine = AProcess.myPid()
        var n = 0
        File("/proc").listFiles()?.forEach { d ->
            val pid = d.name.toIntOrNull() ?: return@forEach
            if (pid == mine) return@forEach
            val cmd = runCatching { String(File(d, "cmdline").readBytes(), Charsets.ISO_8859_1).replace('\u0000', ' ') }.getOrNull() ?: return@forEach
            val ours = cmd.contains("libaemu64") || cmd.contains("libbinderd64") || (cmd.contains("/images/") && (cmd.contains("libqemu") || cmd.contains("libbinderd") || cmd.contains("libdhdrun") || cmd.contains("libglserverd"))) ||
                cmd.startsWith("${ctx.packageName}:binder") || cmd.startsWith("${ctx.packageName}:gl")
            if (ours) { runCatching { AProcess.sendSignal(pid, 9) }; n++ }
        }
        if (n > 0) log("cleaned up processes from previous run: $n")
        return n
    }

    // ------------------------------------------------------------------ служебное

    fun bootSeconds(): Long = if (bootAt == 0L) 0 else ((if (bootDoneAt > 0) bootDoneAt else System.currentTimeMillis()) - bootAt) / 1000

    /**
     * The PATH of 2.3–4.x starts with /sbin: am/pm/input then look up "app_process" there first and qemu gives up
     * ("Error while loading …/sbin/app_process"), so every menu action that ran such a script failed.
     */
    private val open32: Boolean get() = engine == Engine.N64 && img.abi != "arm64-v8a"

    /** open32: the launcher, SystemUI and Settings in machine code, once per image (a marker in run/ remembers it) */
    private fun compileVisibleApps() = runCatching {
        val mark = File(paths.bin, "compiled.speed")
        if (mark.isFile) return@runCatching
        repeat(100) { if (stopping) return@runCatching; Thread.sleep(200) } // the first minute is busy enough
        val home = guestShell("cmd package resolve-activity --brief -c android.intent.category.HOME -a android.intent.action.MAIN | tail -1", 30_000)
            .trim().substringBefore('/').takeIf { it.isNotEmpty() && ' ' !in it }
        val pkgs = listOfNotNull(home, "com.android.systemui", "com.android.settings").distinct()
        var ok = 0
        for (p in pkgs) {
            if (state != State.RUNNING) return@runCatching
            val out = guestShell("cmd package compile -m speed $p", 600_000)
            if ("Success" in out) ok++ else if ("could not be compiled" in out) ok++ else log("compile $p: ${out.trim().lines().lastOrNull()}")
        }
        log("apps compiled to machine code: $ok of ${pkgs.size}")
        if (ok > 0) mark.writeText(pkgs.joinToString(" "))
    }

    fun guestShell(cmd: String, timeoutMs: Long = 60_000): String = lifecycle.run {
        guestRunner.run(listOf("/system/bin/sh", "-c", shellPrefix + cmd), timeoutMs).second
    }

    internal val apkInstallGate = ApkInstallGate()

    internal fun apkInstallGeneration(): Long? = lifecycle.currentGeneration()

    internal fun installApkStream(staged: File, size: Long, generation: Long, timeoutMs: Long = 300_000): ApkStreamResult = lifecycle.runForGeneration(generation) {
        guestRunner.installStream(shellPrefix + "pm install -r --bypass-low-target-sdk-block -S $size", staged, size, timeoutMs)
    }

    internal fun startExternalProcess(builder: ProcessBuilder): Process = lifecycle.create {
        val process = guestLauncher.start(builder)
        externalProcs.add(process)
        Thread({ try { process.waitFor() } finally { externalProcs.remove(process) } }, "vm-external-process").apply { isDaemon = true; start() }
        process
    }

    internal fun startAdbServer(action: () -> Boolean): Boolean {
        var started = false
        lifecycle.ifRunning { if (state == State.RUNNING) started = action() }
        return started
    }

    /** Put in front of every guest shell command (menu, adb, terminal): PATH order and the tool wrappers. */
    val shellPrefix: String get() = "export PATH=/system/bin:/system/xbin:\$PATH; $toolWrappers"

    /**
     * Shell functions that run the firmware's Java tools (am, pm, input, svc…) through app.aemu.stub.Run, which
     * calls System.exit() after main(): left alone, the tool's VM waits forever for its binder thread and the command
     * never returns. Jar and class come from each tool's own script in /system/bin.
     */
    private val toolWrappers: String by lazy {
        // pre-1.0 dalvik reads only dex 013 (aemu-stubs.jar is dex 035): there Run comes from aemu-run013.jar
        val stubs = if (img.api < 3) "/system/framework/aemu-run013.jar" else "/system/framework/aemu-stubs.jar"
        if (!File(paths.root, stubs.removePrefix("/")).isFile) return@lazy ""
        val sb = StringBuilder()
        File(paths.root, "system/bin").listFiles()?.forEach { f ->
            if (!f.isFile || f.length() > 8_000 || !f.name.matches(Regex("[a-z][a-z0-9_]*"))) return@forEach
            val text = runCatching { f.readText(Charsets.ISO_8859_1) }.getOrNull() ?: return@forEach
            val cls = Regex("app_process\\S*\\s+\\S+\\s+([A-Za-z_][\\w.$]*\\.[A-Z][\\w$]*)").find(text)?.groupValues?.get(1) ?: return@forEach
            val jar = Regex("framework/([\\w.-]+\\.jar)").find(text)?.groupValues?.get(1) ?: return@forEach
            sb.append("${f.name}() { CLASSPATH=/system/framework/$jar:$stubs app_process /system/bin app.aemu.stub.Run $cls \"\$@\"; }; ")
        }
        sb.toString()
    }

    fun installApk(apkOnSdcard: String): String = guestShell("pm install -r $apkOnSdcard", 600_000)

    private fun pidOf(p: Process): Int? {
        var c: Class<*>? = p.javaClass
        while (c != null) {
            val f = c.declaredFields.firstOrNull { it.name == "pid" }
            if (f != null) { f.isAccessible = true; return f.getInt(p) }
            c = c.superclass
        }
        return null
    }

    /** What init of 10+ does after reading the property files: ro.product.<what> from the first partition that has
     *  ro.product.<partition>.<what>, and the fingerprint from its parts. Only where the firmware does not say. */
    private fun deriveIdentity(later: MutableMap<String, String>) {
        val all = LinkedHashMap<String, String>()
        for (n in listOf("system/build.prop", "vendor/default.prop", "vendor/build.prop", "odm/etc/build.prop", "odm/build.prop",
            "product/etc/build.prop", "product/build.prop", "product_services/build.prop", "system_ext/etc/build.prop", "system_ext/build.prop")) runCatching {
            File(paths.root, n).takeIf { it.isFile }?.let { f -> PropArea.parseProps(f.readText()).forEach { (k, v) -> all.putIfAbsent(k, v) } }
        }
        // (the files init of 10 reads besides the ones above: the odm's and the product's)
        for (n in listOf("odm/etc/build.prop", "odm/build.prop", "product/etc/build.prop", "product/build.prop", "product_services/build.prop",
            "system_ext/etc/build.prop", "system_ext/build.prop", "mi_ext/etc/build.prop")) runCatching {
            File(paths.root, n).takeIf { it.isFile }?.let { f -> PropArea.parseProps(f.readText()).forEach { (k, v) -> later.putIfAbsent(k, v) } }
        }
        val order = (all["ro.product.property_source_order"] ?: "product,product_services,odm,vendor,system").split(',')
        for (what in listOf("brand", "device", "manufacturer", "model", "name")) {
            if (all.containsKey("ro.product.$what")) continue
            val v = order.firstNotNullOfOrNull { all["ro.product.${it.trim()}.$what"]?.takeIf { s -> s.isNotEmpty() } } ?: continue
            all["ro.product.$what"] = v; later.putIfAbsent("ro.product.$what", v)
        }
        if (!all.containsKey("ro.build.fingerprint")) {
            fun p(k: String) = all[k] ?: "unknown"
            later.putIfAbsent("ro.build.fingerprint", p("ro.product.brand") + "/" + p("ro.product.name") + "/" + p("ro.product.device") + ":" +
                p("ro.build.version.release") + "/" + p("ro.build.id") + "/" + p("ro.build.version.incremental") + ":" + p("ro.build.type") + "/" + p("ro.build.tags"))
        }
    }

    /** the organisation of the platform signing certificate (framework-res.apk, PKCS#7 in META-INF), or null for
     *  AOSP's test keys (O=Android) */
    private fun platformMaker(): String? {
        val apk = File(paths.root, "system/framework/framework-res.apk").takeIf { it.isFile } ?: return null
        java.util.zip.ZipFile(apk).use { z ->
            val e = z.entries().asSequence().firstOrNull { it.name.startsWith("META-INF/") && it.name.uppercase().let { n -> n.endsWith(".RSA") || n.endsWith(".DSA") || n.endsWith(".EC") } } ?: return null
            val cert = z.getInputStream(e).use { java.security.cert.CertificateFactory.getInstance("X.509").generateCertificates(it) }
                .filterIsInstance<java.security.cert.X509Certificate>().firstOrNull() ?: return null
            // RFC 2253 name: "CN=...,O=...,C=..." (a comma inside a value is escaped as "\,")
            val o = Regex("""(?:^|,)O=((?:[^,\\]|\\.)+)""").find(cert.subjectX500Principal.getName("RFC2253"))?.groupValues?.get(1)
                ?.replace(Regex("""\\(.)"""), "$1") ?: return null
            var s = o.trim()
            // legal forms and the trade suffixes vendors put after the maker's name
            val tail = Regex("""(?i)[\s,]+(AB|AG|Inc\.?|Ltd\.?|Co\.?|LLC|GmbH|Corp\.?|Corporation|Limited|Company|S\.A\.|Oy|Mobile|Communications|Electronics|Telecom(munications)?)$""")
            while (true) { val t = s.replace(tail, ""); if (t == s) break; s = t }
            return s.takeIf { it.isNotEmpty() && !it.equals("Android", true) }
        }
    }

    private fun gmtZone(): String {
        val h = java.util.TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 3_600_000
        return if (h == 0) "Etc/GMT" else "Etc/GMT" + (if (h > 0) "-$h" else "+${-h}")
    }

    private fun isLinkTo(f: File, target: String) = runCatching { android.system.Os.readlink(f.absolutePath) == target }.getOrDefault(false)

    private fun stamp() = java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.US).format(java.util.Date())
}
