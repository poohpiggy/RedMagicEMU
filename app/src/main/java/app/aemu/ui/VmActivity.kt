package app.aemu.ui

import app.aemu.R
import androidx.compose.ui.res.stringResource
import android.annotation.SuppressLint
import android.content.Context
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.offset
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.rounded.ScreenRotation
import androidx.compose.material.icons.rounded.Label
import androidx.compose.material.icons.rounded.Speed
import androidx.compose.material.icons.rounded.BatteryStd
import androidx.compose.material.icons.rounded.Cast
import androidx.compose.material.icons.rounded.PhotoCamera
import androidx.compose.material.icons.rounded.Videocam
import androidx.compose.material.icons.rounded.InstallMobile
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.ContentPaste
import androidx.compose.material.icons.rounded.Keyboard
import androidx.compose.material.icons.rounded.RestartAlt
import androidx.compose.material.icons.rounded.Screenshot
import androidx.compose.material.icons.rounded.Settings
import androidx.compose.material.icons.rounded.Apps
import androidx.compose.material.icons.rounded.Sms
import androidx.compose.material.icons.rounded.Call
import androidx.compose.material.icons.rounded.Lan
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Code
import androidx.compose.material.icons.rounded.History
import androidx.compose.material.icons.automirrored.rounded.Send
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.TextButton
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.IntOffset
import kotlin.math.roundToInt
import android.content.ClipData
import android.content.ClipboardManager
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.Download
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.KeyEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.clickable
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.CropSquare
import androidx.compose.material.icons.rounded.Menu
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.PowerSettingsNew
import androidx.compose.material.icons.rounded.Terminal
import androidx.compose.material.icons.rounded.Circle
import androidx.compose.material.icons.rounded.VolumeDown
import androidx.compose.material.icons.rounded.VolumeUp
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.HorizontalFloatingToolbar
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import app.aemu.core.Engine
import app.aemu.core.GuestLog
import app.aemu.core.GuestVm
import app.aemu.core.ImageStore
import app.aemu.core.InputService
import kotlinx.coroutines.delay
import kotlin.concurrent.thread

/** Процесс :vm держит ровно одну машину. */
object VmHost {
    @Volatile var vm: GuestVm? = null
    @Volatile internal var historyRecorder: BootHistoryRecorder? = null
    @Volatile var poweredOff = false
}

class VmActivity : ComponentActivity() {
    private lateinit var vm: GuestVm
    private lateinit var surfaceView: SurfaceView
    private lateinit var guest: GuestScreen
    /** the guest's screen size, for what takes frames off it (VNC, recording) */
    private var guestW = 0
    private var guestH = 0
    private var vnc: app.aemu.core.VncServer? = null
    private var recorder: app.aemu.core.ScreenRecorder? = null
    private val vncOn = androidx.compose.runtime.mutableStateOf(false)
    private val recOn = androidx.compose.runtime.mutableStateOf(false)
    private val grabThread by lazy { android.os.HandlerThread("frame-grab").apply { start() } }
    private var grabScratch: android.graphics.Bitmap? = null
    private lateinit var box: FrameLayout
    private var state by mutableStateOf(GuestVm.State.STOPPED)
    /** the guest shut itself down: the screen stays with its log until the user closes it */
    private var poweredOff by mutableStateOf(false)
    private var unsubscribeState: (() -> Unit)? = null
    private var unsubscribeLog: (() -> Unit)? = null
    private lateinit var bootHistory: BootHistory
    private var previousBootMessage by mutableStateOf<String?>(null)
    private var recorderErrorShown = false
    private var retryPending by mutableStateOf(false)
    private var stopPending by mutableStateOf(false)
    private var stopError by mutableStateOf<String?>(null)
    private var openLog by mutableStateOf(false)
    /** the guest's notification LED and where the guest screen starts (the LED sits just above it) */
    private var led by mutableStateOf<app.aemu.core.Lights.Led?>(null)
    /** this system's setting "Notification light" (the settings sheet changes it while the system runs) */
    private var showLed by mutableStateOf(true)
    private var guestTopPx by mutableStateOf(0)
    private val logLines = mutableStateListOf<String>()

    @SuppressLint("ClickableViewAccessibility")
    override fun attachBaseContext(base: Context) = super.attachBaseContext(app.aemu.AppPrefs.wrap(base))

    /** The panel's fastest mode at the current resolution. Left alone, Android 15+ runs a window at its "normal"
     *  frame-rate category (60 Hz on many phones) as soon as the finger is off the glass, so a guest drawing at 120
     *  is shown with every other frame gone. */
    private fun topRefresh(): Float = runCatching {
        val d = window.decorView.display ?: @Suppress("DEPRECATION") windowManager.defaultDisplay
        val m = d.mode
        d.supportedModes.filter { it.physicalWidth == m.physicalWidth && it.physicalHeight == m.physicalHeight }.maxOf { it.refreshRate }
    }.getOrDefault(60f)

    private fun askTopRefresh() = runCatching {
        val d = @Suppress("DEPRECATION") windowManager.defaultDisplay
        val m = d.mode
        val best = d.supportedModes.filter { it.physicalWidth == m.physicalWidth && it.physicalHeight == m.physicalHeight }.maxByOrNull { it.refreshRate } ?: return@runCatching
        window.attributes = window.attributes.also { it.preferredDisplayModeId = best.modeId; it.preferredRefreshRate = best.refreshRate }
    }

    private fun surfaceRate(sf: android.view.Surface) {
        if (android.os.Build.VERSION.SDK_INT >= 30) runCatching { sf.setFrameRate(topRefresh(), android.view.Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE) }
    }

    /** Views that redraw on every guest frame ask for the panel's top rate (Android 15+ frame-rate categories). */
    private fun viewRate(v: android.view.View) {
        if (android.os.Build.VERSION.SDK_INT >= 35) runCatching { v.setRequestedFrameRate(topRefresh()) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // the overlay pads itself (status bar, navigation bar, keyboard): the terminal stays above the keyboard
        androidx.core.view.WindowCompat.setDecorFitsSystemWindows(window, false)
        lockOrientation()
        loadBadge()
        loadHud()
        val id = intent.getStringExtra(EXTRA_ID)
        val cur = VmHost.vm
        // Do not refresh or modify a historical image before its firmware baseline is verified.
        val img = if (id != null) ImageStore.get(this, id) else cur?.img
        if (img == null) { finish(); return }
        if (cur != null && rejectDifferentRunningVm(cur.img.id, img.id, cur.state)) {
            android.widget.Toast.makeText(this, R.string.single_conflict, android.widget.Toast.LENGTH_LONG).show()
            finish()
            return
        }
        vm = if (cur != null && cur.img.id == img.id) cur
            else GuestVm(applicationContext, img).also {
                it.recoveryMode = intent.getBooleanExtra(EXTRA_RECOVERY, false)
                val recorder = BootHistoryRecorder(BootHistory(filesDir), img.id)
                it.onState(recorder::onState)
                VmHost.poweredOff = false
                VmHost.historyRecorder = recorder
                VmHost.vm = it
            }
        bootHistory = BootHistory(filesDir)
        if (cur == null) {
            previousBootMessage = try {
                when (bootHistory.previous(img.id)) {
                    BootHistory.Status.FAILED -> getString(R.string.previous_boot_failed)
                    BootHistory.Status.STARTING -> getString(R.string.previous_boot_interrupted)
                    else -> null
                }
            } catch (e: Exception) {
                getString(R.string.boot_history_error)
            }
        }
        if (showHistoryWriteErrorOnAttach(VmHost.historyRecorder)) {
            previousBootMessage = getString(R.string.boot_history_error)
            recorderErrorShown = true
        }
        poweredOff = VmHost.poweredOff
        vm.onPower = { reboot, reason -> if (!reboot) VmHost.poweredOff = true; runOnUiThread { if (reboot) rebootVm(reason == "recovery") else { poweredOff = true; requestStop { } } } }
        val s = vm.settings
        showLed = s.showLed
        if (s.keepScreenOn) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        askTopRefresh()

        val (gw, gh) = if (vm.engine == Engine.GB) 480 to 800 else s.width to s.height
        val root = FrameLayout(this).apply { setBackgroundColor(0xff000000.toInt()) }
        box = FrameLayout(this)
        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(h: SurfaceHolder) { surfaceRate(h.surface); vm.surface = h.surface }
            override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) { surfaceRate(h.surface); vm.surface = h.surface }
            override fun surfaceDestroyed(h: SurfaceHolder) { vm.surface = null }
        })
        guestW = gw; guestH = gh
        guest = GuestScreen(this, gw, gh).apply {
            input = vm.input
            withAdmittedFramebuffer = vm::withAdmittedFramebuffer
            openFramebuffer = { app.aemu.core.SafeFramebuffer.open(applicationContext.filesDir, vm.img.id, vm.recoveryMode) }
            if (vm.recoveryMode) pages = 2
        }
        // 5.0+ renders through the standalone glserverd into fb0 (see GuestVm.glUp), shown like the software path
        val useBridge = vm.engine == Engine.KK && s.gpu && !vm.recoveryMode && vm.img.api < 21
        // the arm64 engine's GLES bridge shows SurfaceFlinger's frames in the same view, straight from the GPU; until
        // the first of them (and with the software renderer) the screen is fb0 as before
        val direct = vm.engine == Engine.N64 && vm.kernel.gpuBridge64 && s.gpu && !vm.recoveryMode
        if (direct) { surfaceView.holder.setFixedSize(gw, gh); guest.direct = { app.aemu.core.GlHost.presented() } }
        surfaceView.visibility = if (useBridge || direct) View.VISIBLE else View.GONE
        guest.passthrough = useBridge
        box.addView(surfaceView, FrameLayout.LayoutParams(-1, -1))
        box.addView(guest, FrameLayout.LayoutParams(-1, -1))
        viewRate(surfaceView); viewRate(guest)
        root.addView(box, FrameLayout.LayoutParams(0, 0))
        val overlay = ComposeView(this).apply { setContent { AemuTheme(forceDark = true) { Overlay() } } }
        root.addView(overlay, FrameLayout.LayoutParams(-1, -1))
        setContentView(root)

        // экран гостя вписываем с сохранением пропорций над панелью кнопок
        val navDp = if (!s.showNavBar) 0 else if (s.navSize == 0) 46 else 64
        // the window draws edge to edge (for the overlay's keyboard padding): the guest screen keeps out of the
        // camera cutout and system bars; the keyboard is not counted, so the screen does not jump when it opens
        var safeTop = 0
        var safeBottom = 0
        var safeLeft = 0
        var safeRight = 0
        androidx.core.view.ViewCompat.setOnApplyWindowInsetsListener(root) { v, ins ->
            val bars = ins.getInsets(androidx.core.view.WindowInsetsCompat.Type.systemBars() or androidx.core.view.WindowInsetsCompat.Type.displayCutout())
            if (bars.top != safeTop || bars.bottom != safeBottom || bars.left != safeLeft || bars.right != safeRight) {
                safeTop = bars.top; safeBottom = bars.bottom; safeLeft = bars.left; safeRight = bars.right; v.requestLayout()
            }
            ins
        }
        root.addOnLayoutChangeListener { v, l, t, r, b, _, _, _, _ ->
            val navPx = (navDp * resources.displayMetrics.density).toInt()
            // landscape: the button bar stands at the right edge, so the screen keeps the full height
            val land = r - l > b - t
            val aw = r - l - safeLeft - safeRight - if (land) navPx else 0
            val ah = b - t - safeTop - safeBottom - if (land) 0 else navPx
            if (aw <= 0 || ah <= 0) return@addOnLayoutChangeListener
            val scale = minOf(aw.toFloat() / gw, ah.toFloat() / gh)
            val w = (gw * scale).toInt()
            val h = (gh * scale).toInt()
            val lp = box.layoutParams as FrameLayout.LayoutParams
            val top = safeTop + (ah - h) / 2
            if (guestTopPx != top) guestTopPx = top
            val left = safeLeft + (aw - w) / 2
            if (lp.width != w || lp.height != h || lp.topMargin != top || lp.leftMargin != left) {
                lp.width = w; lp.height = h
                lp.gravity = Gravity.LEFT or Gravity.TOP
                lp.topMargin = top; lp.leftMargin = left
                v.post { box.layoutParams = lp }
            }
        }

        vm.onFrame = { guest.rings++; guest.poke() }
        logLines.addAll(vm.lines().takeLast(200))
        unsubscribeState = vm.observeState { st ->
            runOnUiThread {
                state = st
                if (st == GuestVm.State.PREPARING || st == GuestVm.State.FAILED) retryPending = false
                if (VmHost.historyRecorder?.error != null) {
                    previousBootMessage = getString(R.string.boot_history_error)
                    recorderErrorShown = true
                } else if (recorderErrorShown) {
                    previousBootMessage = null
                    recorderErrorShown = false
                }
            }
        }
        vm.mic.onPermissionNeeded = { runOnUiThread { askMic.launch(android.Manifest.permission.RECORD_AUDIO) } }
        vm.camera.onPermissionNeeded = { runOnUiThread { askCamera.launch(android.Manifest.permission.CAMERA) } }
        led = vm.lights.current
        vm.lights.onChange { l -> runOnUiThread { led = l } }
        unsubscribeLog = vm.onLog { line -> runOnUiThread { logLines.add(line); if (logLines.size > 400) logLines.removeRange(0, logLines.size - 400) } }
        guest.start()
        loadMenuOffset()

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() { vm.input.press(InputService.KEY_BACK) }
        })

        registerShell()
        if (shouldBootOnAttach(vm.state, previousBootMessage != null, VmHost.poweredOff)) startBoot()
    }

    private fun startBoot() {
        try {
            bootHistory.starting(vm.img.id)
        } catch (e: Exception) {
            runOnUiThread { previousBootMessage = getString(R.string.boot_history_error); retryPending = false }
            return
        }
        runOnUiThread { previousBootMessage = null }
        vm.bootAsync()
    }

    /** Debug: adb shell am broadcast -a <applicationId>.SHELL --es cmd "dumpsys power" → run/shell.out */
    private var shellRx: android.content.BroadcastReceiver? = null
    private fun registerShell() {
        if (!app.aemu.BuildConfig.DEBUG) return
        val rx = object : android.content.BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) {
                // --es keys "116d 115 116u": raw guest key codes, d/u = down/up only, bare = press
                i.getStringExtra("keys")?.let { ks ->
                    thread {
                        for (k in ks.split(' ').filter { it.isNotBlank() }) {
                            val code = k.trimEnd('d', 'u').toIntOrNull() ?: continue
                            when { k.endsWith("d") -> vm.input.key(code, true); k.endsWith("u") -> vm.input.key(code, false)
                                else -> { vm.input.key(code, true); Thread.sleep(80); vm.input.key(code, false) } }
                            Thread.sleep(150)
                        }
                    }
                    return
                }
                i.getStringExtra("sms")?.let { body ->
                    // The exported debug receiver retains its legacy RIL-only behavior.
                    thread {
                        val sent = try { vm.ril.sms("+15550001234", body) } catch (error: Exception) {
                            android.util.Log.w("aemu-radio", "Legacy debug SMS failed", error); false
                        }
                        toast(getString(if (sent) R.string.m_sms_submitted else R.string.m_radio_submit_failed))
                    }
                    return
                }
                if (i.getBooleanExtra("shot", false)) { runOnUiThread { screenshot() }; return }
                // --ez frames true --es out <name>: the count of frames shown and the clock, for the testers
                if (i.getBooleanExtra("frames", false)) {
                    val name = i.getStringExtra("out")?.takeIf { it.matches(Regex("[A-Za-z0-9_.-]+")) } ?: "frames.out"
                    java.io.File(vm.paths.bin, name).writeText("shown=${guest.shown} direct=${guest.passthrough} ms=${android.os.SystemClock.uptimeMillis()} rings=${guest.rings} ${guest.paceReport()}\n")
                    return
                }
                if (i.hasExtra("adb")) { val enabled = i.getBooleanExtra("adb", false); thread { try { if (enabled) vm.adb.start() else vm.adb.stop() } catch (e: Exception) { android.util.Log.e("aemu-adb", "ADB operation failed", e) } }; return }
                val cmd = i.getStringExtra("cmd") ?: return
                // своё имя файла на каждую команду: медленная предыдущая команда не затрёт ответ
                val name = i.getStringExtra("out")?.takeIf { it.matches(Regex("[A-Za-z0-9_.-]+")) } ?: "shell.out"
                thread {
                    val out = runCatching { vm.guestShell(cmd, 120_000) }.getOrElse { it.toString() }
                    val tmp = java.io.File(vm.paths.bin, "$name.tmp")
                    tmp.writeText(out + "\n<<done>>\n"); tmp.renameTo(java.io.File(vm.paths.bin, name))
                }
            }
        }
        shellRx = rx
        val f = android.content.IntentFilter(app.aemu.BuildConfig.APPLICATION_ID + ".SHELL")
        if (android.os.Build.VERSION.SDK_INT >= 33) registerReceiver(rx, f, Context.RECEIVER_EXPORTED) else registerReceiver(rx, f)
    }

    // A hardware keyboard (the phone's own, or the PC's through scrcpy) types into the guest: its keys go in as the
    // Linux key codes the guest's layout (dhd-touch.kl, TreeFixer.keyLayout) maps to Android keys.
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        val code = map(keyCode, event) ?: return super.onKeyDown(keyCode, event)
        if (event.repeatCount == 0) vm.input.key(code, true)
        return true
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        val code = map(keyCode, event) ?: return super.onKeyUp(keyCode, event)
        vm.input.key(code, false)
        return true
    }

    private fun map(k: Int, e: KeyEvent): Int? = when (k) {
        KeyEvent.KEYCODE_VOLUME_UP -> InputService.KEY_VOLUMEUP
        KeyEvent.KEYCODE_VOLUME_DOWN -> InputService.KEY_VOLUMEDOWN
        KeyEvent.KEYCODE_MENU -> InputService.KEY_MENU
        KeyEvent.KEYCODE_SEARCH -> InputService.KEY_SEARCH
        // Back stays the host's (leaving the emulator), as before
        KeyEvent.KEYCODE_BACK -> null
        else -> if (::vm.isInitialized && vm.state == GuestVm.State.RUNNING && !e.isSystem) Keys.KEYS[k] else null
    }

    private object Keys {
        /** Android key code → Linux input key code (linux/input-event-codes.h; the pairs of AOSP's Generic.kl) */
        val KEYS: Map<Int, Int> = HashMap<Int, Int>().apply {
            val letters = intArrayOf(30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44)
            for (i in 0 until 26) put(KeyEvent.KEYCODE_A + i, letters[i])
            put(KeyEvent.KEYCODE_0, 11); for (i in 1..9) put(KeyEvent.KEYCODE_0 + i, 1 + i)
            put(KeyEvent.KEYCODE_SPACE, 57); put(KeyEvent.KEYCODE_ENTER, 28); put(KeyEvent.KEYCODE_NUMPAD_ENTER, 28)
            put(KeyEvent.KEYCODE_DEL, 14); put(KeyEvent.KEYCODE_FORWARD_DEL, 111); put(KeyEvent.KEYCODE_TAB, 15)
            put(KeyEvent.KEYCODE_ESCAPE, 1)
            put(KeyEvent.KEYCODE_SHIFT_LEFT, 42); put(KeyEvent.KEYCODE_SHIFT_RIGHT, 54)
            put(KeyEvent.KEYCODE_ALT_LEFT, 56); put(KeyEvent.KEYCODE_ALT_RIGHT, 100)
            put(KeyEvent.KEYCODE_CTRL_LEFT, 29); put(KeyEvent.KEYCODE_CTRL_RIGHT, 97); put(KeyEvent.KEYCODE_CAPS_LOCK, 58)
            put(KeyEvent.KEYCODE_MINUS, 12); put(KeyEvent.KEYCODE_EQUALS, 13); put(KeyEvent.KEYCODE_LEFT_BRACKET, 26)
            put(KeyEvent.KEYCODE_RIGHT_BRACKET, 27); put(KeyEvent.KEYCODE_SEMICOLON, 39); put(KeyEvent.KEYCODE_APOSTROPHE, 40)
            put(KeyEvent.KEYCODE_GRAVE, 41); put(KeyEvent.KEYCODE_BACKSLASH, 43); put(KeyEvent.KEYCODE_COMMA, 51)
            put(KeyEvent.KEYCODE_PERIOD, 52); put(KeyEvent.KEYCODE_SLASH, 53)
            put(KeyEvent.KEYCODE_DPAD_UP, 103); put(KeyEvent.KEYCODE_DPAD_DOWN, 108); put(KeyEvent.KEYCODE_DPAD_LEFT, 105)
            put(KeyEvent.KEYCODE_DPAD_RIGHT, 106); put(KeyEvent.KEYCODE_DPAD_CENTER, 232)
            put(KeyEvent.KEYCODE_MOVE_HOME, 102); put(KeyEvent.KEYCODE_MOVE_END, 107)
            put(KeyEvent.KEYCODE_PAGE_UP, 104); put(KeyEvent.KEYCODE_PAGE_DOWN, 109)
        }
    }

    override fun onResume() {
        super.onResume()
        if (::vm.isInitialized) vm.sensors.resume(true)
    }

    override fun onPause() {
        if (::vm.isInitialized) vm.sensors.resume(false)
        super.onPause()
    }

    override fun onDestroy() {
        unsubscribeState?.invoke()
        unsubscribeState = null
        unsubscribeLog?.invoke()
        unsubscribeLog = null
        vnc?.stop(); vnc = null
        recorder?.let { r -> thread { r.stop() } }; recorder = null
        if (::guest.isInitialized) guest.stop()
        shellRx?.let { runCatching { unregisterReceiver(it) } }
        super.onDestroy()
    }

    private fun requestStop(onSuccess: () -> Unit) {
        if (stopPending) return
        stopPending = true
        stopError = null
        thread {
            stopForUi({ vm.stop() }, {
                runOnUiThread { stopPending = false; onSuccess() }
            }, { error ->
                runOnUiThread {
                    stopPending = false
                    stopError = error.message ?: error.javaClass.simpleName
                }
            })
        }
    }

    private fun stopVm() {
        requestStop {
            VmHost.vm = null
            VmHost.historyRecorder = null
            finishAndRemoveTask()
            thread {
                Thread.sleep(300)
                android.os.Process.killProcess(android.os.Process.myPid())
            }
        }
    }
    private enum class Dlg { NONE, SMS, CALL, BATTERY, ADB, DEVICE, APPS, BADGE, HUD, CAMERA }

    // position of the floating menu button, kept across launches
    private val uiPrefs by lazy { getSharedPreferences("vm_ui", MODE_PRIVATE) }
    private var menuOffset by mutableStateOf(Offset.Zero)
    private fun loadMenuOffset() { menuOffset = Offset(uiPrefs.getFloat("menu_x", 0f), uiPrefs.getFloat("menu_y", 0f)) }
    /**
     * The window keeps one orientation while a machine runs: the phone turning in the hand does not turn the guest's
     * screen. The menu's "Rotate screen" switches it and the choice is kept; the first time it is the orientation
     * the window opened in.
     */
    private fun lockOrientation(toggle: Boolean = false) {
        val now = if (resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE) "l" else "p"
        var o = uiPrefs.getString("orient", null) ?: now
        if (toggle) o = if (o == "l") "p" else "l"
        uiPrefs.edit().putString("orient", o).apply()
        requestedOrientation = if (o == "l") android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            else android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
    }
    /** A label over the screen (default "RedMagicEMU <version> beta") so testers can post screenshots; menu → Badge. */
    private val defaultBadge get() = "RedMagicEMU ${app.aemu.BuildConfig.VERSION_NAME} beta"
    private var badgeOn by mutableStateOf(false)
    private var badgeText by mutableStateOf("")
    private fun loadBadge() { badgeOn = uiPrefs.getBoolean("badge_on", false); badgeText = uiPrefs.getString("badge_text", null) ?: defaultBadge }
    private fun saveBadge() { uiPrefs.edit().putBoolean("badge_on", badgeOn).putString("badge_text", badgeText).apply() }

    @Composable
    private fun BadgeDialog(onDone: () -> Unit) {
        var on by remember { mutableStateOf(badgeOn) }
        var text by remember { mutableStateOf(badgeText) }
        AlertDialog(onDismissRequest = onDone,
            title = { Text(stringResource(R.string.m_badge)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.badge_show), Modifier.weight(1f))
                        Switch(on, { on = it })
                    }
                    OutlinedTextField(text, { text = it }, singleLine = true, enabled = on)
                    TextButton(onClick = { text = defaultBadge }, enabled = on) { Text(stringResource(R.string.badge_default)) }
                }
            },
            confirmButton = { TextButton(onClick = { badgeOn = on; badgeText = text.ifBlank { defaultBadge }; saveBadge(); onDone() }) { Text(stringResource(android.R.string.ok)) } },
            dismissButton = { TextButton(onClick = onDone) { Text(stringResource(android.R.string.cancel)) } })
    }

    /** Performance overlay: which items, in which corner (0 top start, 1 top end, 2 bottom start, 3 bottom end). */
    private var hudOn by mutableStateOf(false)
    private var hudCorner by mutableStateOf(0)
    private var hudItems by mutableStateOf(setOf("fps", "cpu", "ram"))
    private fun loadHud() {
        hudOn = uiPrefs.getBoolean("hud_on", false); hudCorner = uiPrefs.getInt("hud_corner", 0)
        hudItems = uiPrefs.getStringSet("hud_items", null)?.toSet() ?: setOf("fps", "cpu", "ram")
    }
    private fun saveHud() { uiPrefs.edit().putBoolean("hud_on", hudOn).putInt("hud_corner", hudCorner).putStringSet("hud_items", hudItems).apply() }
    private val hud by lazy { PerfHud(this) }

    @Composable
    private fun HudDialog(onDone: () -> Unit) {
        var on by remember { mutableStateOf(hudOn) }
        var corner by remember { mutableStateOf(hudCorner) }
        var items by remember { mutableStateOf(hudItems) }
        val labels = listOf(
            "fps" to R.string.hud_fps, "touch" to R.string.hud_touch, "cpu" to R.string.hud_cpu, "ram" to R.string.hud_ram,
            "phone_ram" to R.string.hud_phone_ram, "procs" to R.string.hud_procs, "freq" to R.string.hud_freq,
            "temp" to R.string.hud_temp, "uptime" to R.string.hud_uptime)
        AlertDialog(onDismissRequest = onDone,
            title = { Text(stringResource(R.string.m_hud)) },
            text = {
                Column(Modifier.verticalScroll(rememberScrollState())) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.badge_show), Modifier.weight(1f))
                        Switch(on, { on = it })
                    }
                    Text(stringResource(R.string.hud_corner), style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(top = 8.dp, bottom = 4.dp))
                    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                        listOf("↖", "↗", "↙", "↘").forEachIndexed { i, l ->
                            SegmentedButton(selected = corner == i, onClick = { corner = i }, shape = SegmentedButtonDefaults.itemShape(i, 4), enabled = on) { Text(l) }
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    for ((k, r) in labels) Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.fillMaxWidth()) {
                        androidx.compose.material3.Checkbox(k in items, { c -> items = if (c) items + k else items - k }, enabled = on)
                        Text(stringResource(r))
                    }
                }
            },
            confirmButton = { TextButton(onClick = { hudOn = on; hudCorner = corner; hudItems = items; saveHud(); onDone() }) { Text(stringResource(android.R.string.ok)) } },
            dismissButton = { TextButton(onClick = onDone) { Text(stringResource(android.R.string.cancel)) } })
    }

    /** The overlay itself: sampled once a second while shown, drawn without taking touches. */
    @Composable
    private fun HudPanel(modifier: Modifier) {
        var lines by remember { mutableStateOf(listOf<String>()) }
        var fpsLine by remember { mutableStateOf<String?>(null) }
        val items = hudItems
        LaunchedEffect(items) {
            while (true) {
                val out = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) {
                    val l = ArrayList<String>()
                    val need = items.any { it == "cpu" || it == "ram" || it == "procs" }
                    val o = if (need) runCatching { hud.ours() }.getOrNull() else null
                    if ("touch" in items) l += if (::guest.isInitialized && guest.touchMs >= 0) "Touch %d ms".format(guest.touchMs) else "Touch -"
                    if ("cpu" in items && o != null) l += "CPU %.0f%%".format(o.cpuPct)
                    if ("ram" in items && o != null) l += "RAM %d MB".format(o.rssMb)
                    if ("phone_ram" in items) l += "Free %d MB".format(runCatching { hud.phoneFreeMb() }.getOrDefault(0L))
                    if ("procs" in items && o != null) l += "Procs %d".format(o.procs)
                    if ("freq" in items) l += "CPU %d MHz".format(hud.maxFreqMhz())
                    if ("temp" in items) l += runCatching { hud.batteryTempC() }.getOrNull()?.let { "Temp %.1f°C".format(it) } ?: "Temp -"
                    if ("uptime" in items && ::vm.isInitialized && vm.bootDoneAt > 0) {
                        val sec = (System.currentTimeMillis() - vm.bootDoneAt) / 1000
                        l += "Up %d:%02d:%02d".format(sec / 3600, sec / 60 % 60, sec % 60)
                    }
                    l
                }
                lines = out
                delay(1000)
            }
        }
        // FPS on its own, 4 times a second (GuestScreen counts it over 250 ms windows)
        LaunchedEffect(items) {
            fpsLine = null
            while ("fps" in items) {
                fpsLine = "FPS %.0f".format(if (::guest.isInitialized) guest.fps else 0f)
                delay(250)
            }
        }
        val shown = listOfNotNull(fpsLine) + lines
        if (shown.isEmpty()) return
        Column(modifier.background(androidx.compose.ui.graphics.Color.Black.copy(alpha = 0.55f), MaterialTheme.shapes.small)
            .padding(horizontal = 8.dp, vertical = 4.dp)) {
            for (l in shown) Text(l, color = androidx.compose.ui.graphics.Color.White, style = MaterialTheme.typography.labelMedium,
                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace, maxLines = 1)
        }
    }

    private fun saveMenuOffset() { uiPrefs.edit().putFloat("menu_x", menuOffset.x).putFloat("menu_y", menuOffset.y).apply() }

    private val askMic = registerForActivityResult(ActivityResultContracts.RequestPermission()) { ok -> if (ok) vm.mic.permissionGranted() }

    private val askCamera = registerForActivityResult(ActivityResultContracts.RequestPermission()) { }

    private fun toast(msg: String) = runOnUiThread { Toast.makeText(this, msg, Toast.LENGTH_SHORT).show() }

    private fun guestAsync(cmd: String, done: ((String) -> Unit)? = null) {
        thread { val out = runCatching { vm.guestShell(cmd, 60_000) }.getOrElse { it.toString() }; done?.invoke(out) }
    }

    /** Quote for the guest's /system/bin/sh. */
    private fun q(s: String) = "'" + s.replace("'", "'\\''") + "'"

    private fun pasteToGuest() {
        val text = getSystemService(ClipboardManager::class.java).primaryClip?.getItemAt(0)?.coerceToText(this)?.toString().orEmpty()
        if (text.isEmpty()) { toast(getString(R.string.m_clip_empty)); return }
        // Normalize line endings and truncate extreme paste payloads
        val lines = text.replace("\r\n", "\n").replace('\r', '\n').split('\n')
        val safeLines = if (lines.size > 150) lines.take(150) else lines
        // "input text" types only what the virtual keyboard map has (KeyCharacterMap.getEvents is null otherwise):
        // other text goes through the guest's clipboard and Ctrl+V (app.aemu.stub.TypeText; aemu-stubs.jar is 4.2+)
        if (vm.img.api >= 17 && safeLines.any { l -> l.any { it.code !in 0x20..0x7e } }) {
            val b64 = android.util.Base64.encodeToString(safeLines.joinToString("\n").toByteArray(), android.util.Base64.NO_WRAP)
            guestAsync("CLASSPATH=/system/framework/aemu-stubs.jar app_process /system/bin app.aemu.stub.TypeText $b64")
            return
        }
        val script = buildString {
            for ((i, line) in safeLines.withIndex()) {
                if (line.isNotEmpty()) {
                    val escaped = line.replace(" ", "%s")
                    append("input text ").append(q(escaped)).append("; ")
                }
                if (i < safeLines.lastIndex) {
                    append("input keyevent 66; ")
                }
            }
        }
        guestAsync(script)
    }

    /**
     * The frame is taken on the host (the fb0 copy, or the GPU bridge's surface through PixelCopy): `screencap` does
     * not exist before 4.0 and cannot read the bridge's frames. The PNG lands in the guest's Pictures folder.
     */
    private fun screenshot() {
        val name = "RedMagicEMU-" + java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.US).format(java.util.Date()) + ".png"
        val hostDir = app.aemu.core.Sdcard.hostDir(vm.paths)?.let { java.io.File(it, "Pictures") }
        if (hostDir == null) { toast(getString(R.string.m_failed)); return }
        fun save(b: android.graphics.Bitmap) = thread {
            val ok = runCatching {
                hostDir.mkdirs()
                java.io.File(hostDir, name).outputStream().use { b.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
            }.isSuccess
            toast(if (ok) getString(R.string.m_screenshot_saved, vm.img.sdcardPath.trimEnd('/') + "/Pictures/$name") else getString(R.string.m_failed))
            // the guest's media scanner picks the file up for its gallery
            if (ok) guestAsync("am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file://${vm.img.sdcardPath.trimEnd('/')}/Pictures/$name")
        }
        guest.snapshot()?.let { save(it); return }
        if (surfaceView.width <= 0 || surfaceView.height <= 0) { toast(getString(R.string.m_failed)); return }
        val b = android.graphics.Bitmap.createBitmap(surfaceView.width, surfaceView.height, android.graphics.Bitmap.Config.ARGB_8888)
        android.view.PixelCopy.request(surfaceView, b, { r ->
            if (r == android.view.PixelCopy.SUCCESS) save(b) else toast(getString(R.string.m_failed))
        }, android.os.Handler(mainLooper))
    }

    /**
     * The current frame at the guest's resolution: the fb0 copy, or the GPU bridge's surface through PixelCopy
     * (waited for here, so not to be called on the main thread).
     */
    private fun grabFrame(into: android.graphics.Bitmap): Boolean {
        val dst = android.graphics.Rect(0, 0, into.width, into.height)
        guest.snapshot()?.let { android.graphics.Canvas(into).drawBitmap(it, null, dst, null); it.recycle(); return true }
        val sw = surfaceView.width
        val sh = surfaceView.height
        if (sw <= 0 || sh <= 0 || !surfaceView.holder.surface.isValid) return false
        val tmp = grabScratch?.takeIf { it.width == sw && it.height == sh }
            ?: android.graphics.Bitmap.createBitmap(sw, sh, android.graphics.Bitmap.Config.ARGB_8888).also { grabScratch = it }
        val done = java.util.concurrent.CountDownLatch(1)
        var ok = false
        runCatching {
            android.view.PixelCopy.request(surfaceView, tmp, { r -> ok = r == android.view.PixelCopy.SUCCESS; done.countDown() },
                android.os.Handler(grabThread.looper))
        }.onFailure { return false }
        if (!done.await(500, java.util.concurrent.TimeUnit.MILLISECONDS) || !ok) return false
        android.graphics.Canvas(into).drawBitmap(tmp, null, dst, null)
        return true
    }

    /** VNC on/off. Each session gets a password of its own, shown here and in the log. */
    private fun toggleVnc() {
        vnc?.let { it.stop(); vnc = null; vncOn.value = false; return }
        if (guestW <= 0 || guestH <= 0) { toast(getString(R.string.m_failed)); return }
        val password = (1..8).map { "abcdefghijkmnpqrstuvwxyz23456789".random() }.joinToString("")
        val bmp = android.graphics.Bitmap.createBitmap(guestW, guestH, android.graphics.Bitmap.Config.ARGB_8888)
        val server = app.aemu.core.VncServer(app.aemu.core.VncServer.DEFAULT_PORT, password, guestW, guestH, vm.img.name,
            { px -> if (grabFrame(bmp)) { bmp.getPixels(px, 0, guestW, 0, 0, guestW, guestH); true } else false }, vm.input, vm::log)
        if (!server.start()) { toast(getString(R.string.m_failed)); return }
        vnc = server; vncOn.value = true
        val ip = runCatching {
            java.net.NetworkInterface.getNetworkInterfaces().asSequence().filter { it.isUp && !it.isLoopback }
                .flatMap { it.inetAddresses.asSequence() }.firstOrNull { it is java.net.Inet4Address }?.hostAddress
        }.getOrNull() ?: "127.0.0.1"
        val where = getString(R.string.m_vnc_started, "$ip:${app.aemu.core.VncServer.DEFAULT_PORT}", password)
        vm.log("vnc: $where")
        runOnUiThread { Toast.makeText(this, where, Toast.LENGTH_LONG).show() }
    }

    /** Screen recording on/off: an MP4 in the Movies folder of the guest's card (the phone's shared folder). */
    private fun toggleRecording() {
        recorder?.let { r ->
            recorder = null; recOn.value = false
            thread {
                r.stop()
                toast(getString(R.string.m_screenshot_saved, vm.img.sdcardPath.trimEnd('/') + "/Movies/" + r.file.name))
                guestAsync("am broadcast -a android.intent.action.MEDIA_SCANNER_SCAN_FILE -d file://${vm.img.sdcardPath.trimEnd('/')}/Movies/${r.file.name}")
            }
            return
        }
        val dir = app.aemu.core.Sdcard.hostDir(vm.paths)?.let { java.io.File(it, "Movies") }
        if (dir == null || guestW <= 0 || guestH <= 0) { toast(getString(R.string.m_failed)); return }
        val name = "RedMagicEMU-" + java.text.SimpleDateFormat("yyyyMMdd-HHmmss", java.util.Locale.US).format(java.util.Date()) + ".mp4"
        val r = app.aemu.core.ScreenRecorder(java.io.File(dir, name), guestW, guestH, ::grabFrame, vm::log)
        if (!r.start()) { toast(getString(R.string.m_failed)); return }
        recorder = r; recOn.value = true
    }

    private var apkPickerGeneration: Long? = null

    // APK picked on the host: for API 36, stream a private staged copy into the guest package manager.
    private val pickApk = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val generation = apkPickerGeneration.also { apkPickerGeneration = null }
        if (uri == null) return@registerForActivityResult
        if (vm.img.api == 36) {
            if (generation == null) { toast(getString(R.string.m_failed)); return@registerForActivityResult }
            if (!vm.apkInstallGate.tryAcquire()) { toast(getString(R.string.m_failed)); return@registerForActivityResult }
            toast(getString(R.string.m_install_apk_start))
            thread {
                var staged: java.io.File? = null
                try {
                    staged = java.io.File.createTempFile("rme-apk-", ".tmp", cacheDir)
                    val size = (contentResolver.openInputStream(uri) ?: throw java.io.IOException("Cannot open APK"))
                        .use { app.aemu.core.stageApk(it, staged) }
                    val result = vm.installApkStream(staged, size, generation)
                    if (result.success) toast("Success")
                    else toast(result.output.trim().lines().firstOrNull { it.startsWith("Failure") || it.startsWith("Error") }
                        ?.take(200) ?: getString(R.string.m_failed))
                } catch (e: Exception) {
                    vm.log("apk install: ${e.javaClass.simpleName}")
                    toast(getString(R.string.m_failed))
                } finally {
                    staged?.let { if (it.exists() && !it.delete()) vm.log("apk install: temporary file cleanup failed") }
                    vm.apkInstallGate.release()
                }
            }
            return@registerForActivityResult
        }
        val dir = app.aemu.core.Sdcard.hostDir(vm.paths)
        if (dir == null) { toast(getString(R.string.m_failed)); return@registerForActivityResult }
        toast(getString(R.string.m_install_apk_start))
        thread {
            val ok = runCatching {
                contentResolver.openInputStream(uri)!!.use { i -> java.io.File(dir, "aemu-install.apk").outputStream().use { o -> i.copyTo(o) } }
            }.isSuccess
            if (!ok) { toast(getString(R.string.m_failed)); return@thread }
            // 14+ refuses apps targeting below 23 (INSTALL_FAILED_DEPRECATED_SDK_VERSION); its pm has a switch for that
            val flags = if (vm.img.api >= 34) "-r --bypass-low-target-sdk-block " else "-r "
            val out = runCatching { vm.guestShell("pm install " + flags + vm.img.sdcardPath.trimEnd('/') + "/aemu-install.apk", 300_000) }.getOrElse { it.toString() }
            runCatching { app.aemu.core.TreeFixer(this@VmActivity, vm.paths, vm.img, vm::log).seedOwners() }
            toast(out.trim().lines().lastOrNull { it.isNotBlank() }?.take(200) ?: getString(R.string.m_failed))
        }
    }

    private fun pickApkForGuest() {
        if (vm.img.api == 36) {
            apkPickerGeneration = vm.apkInstallGeneration()
            if (apkPickerGeneration == null) { toast(getString(R.string.m_failed)); return }
        }
        pickApk.launch(arrayOf("application/vnd.android.package-archive", "application/octet-stream", "*/*"))
    }

    private fun sendSms(from: String, body: String) {
        val generation = vm.radioEventGeneration()
        if (generation == null) { toast(getString(R.string.m_radio_submit_failed)); return }
        thread {
            val result = try { vm.sendVirtualSms(from, body, generation) } catch (error: Exception) {
                android.util.Log.w("aemu-radio", "Virtual SMS submission failed", error); null
            }
            toast(getString(if (result == app.aemu.core.RadioSubmitResult.SUBMITTED) R.string.m_sms_submitted else R.string.m_radio_submit_failed))
        }
    }

    @Composable
    private fun SmsDialog(onDone: () -> Unit) {
        var from by remember { mutableStateOf("+10000000000") }
        var body by remember { mutableStateOf("Hello from RedMagicEMU") }
        var tooLong by remember { mutableStateOf(false) }
        AlertDialog(onDismissRequest = onDone,
            title = { Text(stringResource(R.string.m_sms)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedTextField(from, { from = it }, label = { Text(stringResource(R.string.m_sms_from)) }, singleLine = true)
                    OutlinedTextField(body, {
                        body = it
                        if (tooLong) tooLong = !app.aemu.core.VirtualSmsDeliver.isTextLengthSupported(it)
                    }, label = { Text(stringResource(R.string.m_sms_body)) },
                        isError = tooLong,
                        supportingText = if (tooLong) {{ Text(stringResource(R.string.m_sms_too_long)) }} else null)
                }
            },
            confirmButton = { Button(onClick = {
                if (app.aemu.core.VirtualSmsDeliver.isTextLengthSupported(body)) {
                    sendSms(from, body)
                    onDone()
                } else tooLong = true
            }) { Text(stringResource(R.string.m_send)) } },
            dismissButton = { OutlinedButton(onClick = onDone) { Text(stringResource(R.string.close)) } })
    }

    /** The firmware's apps: packages with a MAIN activity, labels and icons read on the host from their APKs. */
    @Composable
    private fun AppsDialog(onDone: () -> Unit) {
        var apps by remember { mutableStateOf<List<app.aemu.core.GuestApp>?>(null) }
        LaunchedEffect(Unit) {
            // the package list first: the runner keeps 256 KB of output and the resolver dump alone can be more
            guestAsync("pm list packages -f; echo ===AEMU===; dumpsys package r") { out ->
                val paths = app.aemu.core.GuestApps.parsePaths(out.substringBefore("===AEMU==="))
                val main = app.aemu.core.GuestApps.parseMain(out.substringAfter("===AEMU===", ""))
                val list = main.map { app.aemu.core.GuestApps.load(this@VmActivity, vm.paths.root, it, paths[it]) }
                    .sortedWith(compareBy({ it.label == it.pkg }, { it.label.lowercase() }))
                vm.log("apps: ${main.size} launchable, ${paths.size} paths, ${list.count { it.label != it.pkg }} labelled; ${app.aemu.core.GuestApps.lastError}")
                runOnUiThread { apps = list }
            }
        }
        GuestAppsSheet(apps, onDismiss = onDone, onLaunch = { a ->
            onDone()
            guestAsync("monkey -p ${q(a.pkg)} -c android.intent.category.LAUNCHER 1") { out ->
                if (out.contains("No activities found")) toast(getString(R.string.m_apps_none))
            }
        })
    }

    @Composable
    private fun DeviceDialog(onDone: () -> Unit) {
        var airplane by remember { mutableStateOf(false) }
        var awake by remember { mutableStateOf(false) }
        val ns = if (vm.img.api >= 17) "global" else "system"
        // the switches show what the guest has now
        LaunchedEffect(Unit) {
            guestAsync("settings get $ns airplane_mode_on; settings get $ns stay_on_while_plugged_in") { out ->
                val v = out.lines().map { it.trim() }.filter { it.isNotEmpty() }
                runOnUiThread { airplane = v.getOrNull(0) == "1"; awake = (v.getOrNull(1)?.toIntOrNull() ?: 0) != 0 }
            }
        }
        AlertDialog(onDismissRequest = onDone,
            title = { Text(stringResource(R.string.m_device)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.m_airplane), Modifier.weight(1f))
                        Switch(airplane, { v ->
                            airplane = v
                            guestAsync("settings put $ns airplane_mode_on ${if (v) 1 else 0}; " +
                                "am broadcast -a android.intent.action.AIRPLANE_MODE --ez state $v")
                        })
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.m_stay_awake), Modifier.weight(1f))
                        Switch(awake, { v ->
                            awake = v
                            guestAsync("settings put $ns stay_on_while_plugged_in ${if (v) 7 else 0}; svc power stayon ${if (v) "true" else "false"}")
                        })
                    }
                }
            },
            confirmButton = { Button(onClick = onDone) { Text(stringResource(R.string.close)) } })
    }

    @Composable
    private fun AdbDialog(onDone: () -> Unit) {
        var on by remember { mutableStateOf(vm.adb.running) }
        var busy by remember { mutableStateOf(false) }
        var error by remember { mutableStateOf<String?>(null) }
        AlertDialog(onDismissRequest = onDone,
            title = { Text(stringResource(R.string.m_adb)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.m_adb_enable), Modifier.weight(1f))
                        Switch(on, { v ->
                            busy = true
                            error = null
                            thread {
                                val failure = try { if (v) vm.adb.start() else vm.adb.stop(); null } catch (e: Exception) { e.message ?: e.javaClass.simpleName }
                                runOnUiThread { on = vm.adb.running; busy = false; error = failure }
                            }
                        }, enabled = !busy)
                    }
                    error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
                    if (on) {
                        val ips = app.aemu.core.AdbServer.addresses()
                        for (ip in ips.ifEmpty { listOf("127.0.0.1") })
                            Text("adb connect $ip:${vm.adb.port}", style = MaterialTheme.typography.bodyLarge,
                                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace)
                    }
                    Text(stringResource(R.string.m_adb_hint), style = MaterialTheme.typography.bodySmall)
                }
            },
            confirmButton = { Button(onClick = onDone) { Text(stringResource(R.string.close)) } })
    }

    @Composable
    private fun CallDialog(onDone: () -> Unit) {
        var from by remember { mutableStateOf("+10000000000") }
        AlertDialog(onDismissRequest = onDone,
            title = { Text(stringResource(R.string.m_call)) },
            text = { OutlinedTextField(from, { from = it }, label = { Text(stringResource(R.string.m_sms_from)) }, singleLine = true) },
            confirmButton = { Button(onClick = {
                val number = from.trim()
                val generation = vm.radioEventGeneration()
                thread {
                    val result = generation?.let {
                        try { vm.ringVirtualCall(number, it) } catch (error: Exception) {
                            android.util.Log.w("aemu-radio", "Virtual call submission failed", error); null
                        }
                    }
                    toast(getString(if (result == app.aemu.core.RadioSubmitResult.SUBMITTED) R.string.m_sms_submitted else R.string.m_radio_submit_failed))
                }
                onDone()
            }) { Text(stringResource(R.string.m_call_ring)) } },
            dismissButton = { OutlinedButton(onClick = onDone) { Text(stringResource(R.string.close)) } })
    }

    @Composable
    private fun BatteryDialog(onDone: () -> Unit) {
        var level by remember { mutableStateOf(80f) }
        var charging by remember { mutableStateOf(true) }
        // start from the guest's own battery state (sysfs or dumpsys)
        LaunchedEffect(Unit) {
            val cmd = "cat /sys/class/power_supply/battery/capacity 2>/dev/null; " +
                "cat /sys/class/power_supply/ac/online 2>/dev/null; " +
                "dumpsys battery 2>/dev/null"
            guestAsync(cmd) { out ->
                val lines = out.lines().map { it.trim() }
                val sysCap = lines.getOrNull(0)?.toFloatOrNull()
                val sysAc = lines.getOrNull(1)?.toIntOrNull()
                fun field(n: String) = out.lineSequence().map { it.trim() }.firstOrNull { it.startsWith("$n:") }?.substringAfter(':')?.trim()
                val l = sysCap ?: field("level")?.toFloatOrNull()
                val ac = if (sysAc != null) sysAc == 1 else (field("AC powered") == "true" || field("USB powered") == "true")
                runOnUiThread {
                    if (l != null) level = l.coerceIn(0f, 100f)
                    charging = ac
                }
            }
        }
        AlertDialog(onDismissRequest = onDone,
            title = { Text(stringResource(R.string.m_battery)) },
            text = {
                Column {
                    Text("${level.toInt()}%")
                    Slider(level, { level = it }, valueRange = 0f..100f)
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.m_charging), Modifier.weight(1f)); Switch(charging, { charging = it })
                    }
                }
            },
            confirmButton = { Button(onClick = {
                val lvl = level.toInt()
                val ac = if (charging) 1 else 0
                val cmd = "echo $lvl > /sys/class/power_supply/battery/capacity 2>/dev/null; " +
                    "echo $ac > /sys/class/power_supply/ac/online 2>/dev/null; " +
                    "dumpsys battery set level $lvl 2>/dev/null; " +
                    "dumpsys battery set ac $ac 2>/dev/null; " +
                    "dumpsys battery set usb 0 2>/dev/null"
                guestAsync(cmd)
                onDone()
            }) { Text(stringResource(R.string.m_apply)) } },
            dismissButton = { OutlinedButton(onClick = {
                guestAsync("dumpsys battery reset 2>/dev/null")
                onDone()
            }) { Text(stringResource(R.string.m_reset)) } })
    }

    private fun rebootVm(recovery: Boolean = false) {
        val id = vm.img.id
        requestStop {
            VmHost.vm = null
            VmHost.historyRecorder = null
            // The GL bridge requires a fresh :vm process for a reboot.
            restartProcess(id, recovery)
        }
    }
    private fun restartProcess(id: String, recovery: Boolean = false) {
        val i = Intent(this, VmActivity::class.java).putExtra(EXTRA_ID, id).putExtra(EXTRA_RECOVERY, recovery)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        val pi = android.app.PendingIntent.getActivity(this, 1, i, android.app.PendingIntent.FLAG_IMMUTABLE or android.app.PendingIntent.FLAG_CANCEL_CURRENT)
        (getSystemService(Context.ALARM_SERVICE) as android.app.AlarmManager).set(android.app.AlarmManager.RTC, System.currentTimeMillis() + 400, pi)
        finishAndRemoveTask()
        android.os.Process.killProcess(android.os.Process.myPid())
    }

    // ------------------------------------------------------------------ интерфейс поверх экрана

    @Composable
    private fun Overlay() {
        var showLog by remember { mutableStateOf(false) }
        LaunchedEffect(openLog) { if (openLog) { showLog = true; openLog = false } }
        var showTerm by remember { mutableStateOf(false) }
        var menu by remember { mutableStateOf(false) }
        var dialog by remember { mutableStateOf(Dlg.NONE) }
        // recovery is "running" too, but has no Android behind am/pm/settings
        val running = state == GuestVm.State.RUNNING && !vm.recoveryMode
        var seconds by remember { mutableStateOf(0L) }
        var bootElapsedMs by remember { mutableStateOf(0L) }
        var waitDismissedFor by remember { mutableStateOf(-1L) }
        var frames by remember { mutableStateOf(0L) }
        LaunchedEffect(Unit) { while (true) { if (vm.recoveryMode) guest.format = vm.recoveryFormat else if (vm.engine == app.aemu.core.Engine.N64) { guest.format = if (vm.fb565 || vm.fbLegacy565) 0 else 1; guest.onePage = vm.fb565 && !vm.fbLegacy565 }; seconds = vm.bootSeconds(); bootElapsedMs = if (vm.bootStartedElapsed > 0L) (android.os.SystemClock.elapsedRealtime() - vm.bootStartedElapsed).coerceAtLeast(0L) else 0L; frames = if (vm.glInApp) dev.lk.m7sense.GlBridge.frames() else maxOf(guest.rings, guest.shown); delay(500) } }
        // как только гость начал рисовать — карточку убираем, остаётся маленький индикатор
        // (frames shown on the GPU path of the arm64 kernels never ring onFrame: the card hid their boot animation)
        val drawing = frames > 30
        val unconfirmed = bootWarningVisible(state, bootElapsedMs, waitDismissedFor == vm.bootStartedElapsed)
        androidx.compose.foundation.layout.BoxWithConstraints(Modifier.fillMaxSize()) {
            // the menu button stays on screen: 40 dp button + 8 dp padding, anchored at the top end
            val keep = with(androidx.compose.ui.platform.LocalDensity.current) { 56.dp.toPx() }
            val maxX = (constraints.maxWidth - keep).coerceAtLeast(0f)
            val maxY = (constraints.maxHeight - keep * 2).coerceAtLeast(0f)
            val rtl = androidx.compose.ui.platform.LocalLayoutDirection.current == androidx.compose.ui.unit.LayoutDirection.Rtl
            fun clamp(o: Offset) = Offset(if (rtl) o.x.coerceIn(0f, maxX) else o.x.coerceIn(-maxX, 0f), o.y.coerceIn(0f, maxY))
            LaunchedEffect(maxX, maxY) { menuOffset = clamp(menuOffset) }
            led?.takeIf { it.lit && showLed }?.let { LedIndicator(it, Modifier.align(Alignment.TopCenter)) }
            // карточка загрузки
            AnimatedVisibility(
                visible = state != GuestVm.State.RUNNING && !showLog && (!(drawing && state == GuestVm.State.BOOTING) || unconfirmed),
                enter = fadeIn(), exit = fadeOut(),
                modifier = Modifier.align(Alignment.Center),
            ) { BootCard(seconds, unconfirmed) { waitDismissedFor = vm.bootStartedElapsed } }

            if (hudOn && !showLog && !showTerm) {
                val a = when (hudCorner) { 1 -> Alignment.TopEnd; 2 -> Alignment.BottomStart; 3 -> Alignment.BottomEnd; else -> Alignment.TopStart }
                // below the badge when both are in the top start corner; clear of the menu button and nav bar in the others
                val below = if (hudCorner == 0 && badgeOn) 36.dp else 0.dp
                HudPanel(Modifier.align(a).displayCutoutPadding().statusBarsPadding().navigationBarsPadding()
                    .padding(start = 8.dp, top = 8.dp + below + (if (hudCorner == 1) 48.dp else 0.dp), end = if (hudCorner % 2 == 1) 64.dp else 8.dp, bottom = 8.dp))
            }
            if (badgeOn && !showLog && !showTerm && !(drawing && state == GuestVm.State.BOOTING && vm.screenUpAt == 0L)) {
                // a plain Box: Material's Surface takes the touches, this lets them through to the guest screen
                Box(Modifier.align(Alignment.TopStart).displayCutoutPadding().statusBarsPadding().padding(8.dp)
                    .background(androidx.compose.ui.graphics.Color.Black.copy(alpha = 0.55f), MaterialTheme.shapes.small)) {
                    Text(badgeText, color = androidx.compose.ui.graphics.Color.White, style = MaterialTheme.typography.labelLarge,
                        maxLines = 1, modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp))
                }
            }
            if (shouldShowBootNotice(state) && (drawing || state == GuestVm.State.RUNNING) && !showLog) {
                Surface(shape = CircleShape, color = MaterialTheme.colorScheme.surfaceContainerHigh.copy(alpha = 0.9f),
                    modifier = Modifier.align(Alignment.TopStart).displayCutoutPadding().statusBarsPadding().padding(8.dp)) {
                    Row(Modifier.padding(horizontal = 12.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                        LoadingIndicator(Modifier.size(24.dp))
                        Spacer(Modifier.width(6.dp))
                        Text(stringResource(R.string.vm_booting_short, seconds.toInt()), style = MaterialTheme.typography.labelMedium)
                    }
                }
            }

            // журнал
            AnimatedVisibility(visible = showLog, enter = fadeIn(), exit = fadeOut(), modifier = Modifier.fillMaxSize()) {
                LogPanel(onClose = { showLog = false })
            }
            AnimatedVisibility(visible = showTerm, enter = fadeIn(), exit = fadeOut(), modifier = Modifier.fillMaxSize()) {
                TerminalPanel(onClose = { showTerm = false })
            }

            // top menu button (hidden while the log is open so it does not cover the log toolbar); drag to move it
            if (!showLog && !showTerm) Box(Modifier.align(Alignment.TopEnd).displayCutoutPadding().statusBarsPadding().padding(8.dp)
                .offset { IntOffset(menuOffset.x.roundToInt(), menuOffset.y.roundToInt()) }
                .pointerInput(Unit) {
                    detectDragGestures(onDragEnd = { saveMenuOffset() }) { ch, d -> ch.consume(); menuOffset = clamp(menuOffset + d) }
                }) {
                FilledTonalIconButton(onClick = { menu = true }, modifier = Modifier.size(40.dp)) {
                    Icon(Icons.Rounded.MoreVert, contentDescription = stringResource(R.string.menu))
                }
                DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                    DropdownMenuItem(text = { Text(stringResource(R.string.log)) }, leadingIcon = { Icon(Icons.Rounded.Terminal, null) },
                        onClick = { menu = false; showLog = true })
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_terminal)) }, leadingIcon = { Icon(Icons.Rounded.Code, null) },
                        enabled = running, onClick = { menu = false; showTerm = true })
                    // the machine keeps running behind its notification
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_minimize)) }, leadingIcon = { Icon(Icons.Rounded.Close, null) },
                        onClick = { menu = false; moveTaskToBack(true) })
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_rotate)) }, leadingIcon = { Icon(Icons.Rounded.ScreenRotation, null) },
                        onClick = { menu = false; lockOrientation(toggle = true) })
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_badge)) }, leadingIcon = { Icon(Icons.Rounded.Label, null) },
                        onClick = { menu = false; dialog = Dlg.BADGE })
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_hud)) }, leadingIcon = { Icon(Icons.Rounded.Speed, null) },
                        onClick = { menu = false; dialog = Dlg.HUD })
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text(stringResource(R.string.vol_up)) }, leadingIcon = { Icon(Icons.Rounded.VolumeUp, null) },
                        onClick = { vm.input.press(InputService.KEY_VOLUMEUP) })
                    DropdownMenuItem(text = { Text(stringResource(R.string.vol_down)) }, leadingIcon = { Icon(Icons.Rounded.VolumeDown, null) },
                        onClick = { vm.input.press(InputService.KEY_VOLUMEDOWN) })
                    DropdownMenuItem(text = { Text(stringResource(R.string.power)) }, leadingIcon = { Icon(Icons.Rounded.PowerSettingsNew, null) },
                        onClick = { menu = false; vm.input.press(InputService.KEY_POWER) })
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_power_menu)) }, leadingIcon = { Icon(Icons.Rounded.PowerSettingsNew, null) },
                        onClick = { menu = false; vm.input.press(InputService.KEY_POWER, 1500) })
                    // long-press Menu makes 2.x–4.x call InputMethodManager.toggleSoftInput: the firmware's own keyboard
                    if (!vm.recoveryMode) {
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_keyboard)) }, leadingIcon = { Icon(Icons.Rounded.Keyboard, null) },
                            enabled = running, onClick = {
                                menu = false
                                // 5.0+ hides the soft keyboard while it sees hardware keys and has no long-Menu toggle
                                if (vm.img.api >= 21) guestAsync("settings put secure show_ime_with_hard_keyboard 1")
                                else vm.input.press(InputService.KEY_MENU, 1000)
                            })
                        HorizontalDivider()
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_paste)) }, leadingIcon = { Icon(Icons.Rounded.ContentPaste, null) },
                            enabled = running, onClick = { menu = false; pasteToGuest() })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_sms)) }, leadingIcon = { Icon(Icons.Rounded.Sms, null) },
                            enabled = running, onClick = { menu = false; dialog = Dlg.SMS })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_call)) }, leadingIcon = { Icon(Icons.Rounded.Call, null) },
                            enabled = running, onClick = { menu = false; dialog = Dlg.CALL })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_battery)) }, leadingIcon = { Icon(Icons.Rounded.BatteryStd, null) },
                            enabled = running, onClick = { menu = false; dialog = Dlg.BATTERY })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_install_apk)) }, leadingIcon = { Icon(Icons.Rounded.InstallMobile, null) },
                            enabled = running, onClick = { menu = false; pickApkForGuest() })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_camera)) }, leadingIcon = { Icon(Icons.Rounded.PhotoCamera, null) },
                            onClick = { menu = false; dialog = Dlg.CAMERA })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_device)) }, leadingIcon = { Icon(Icons.Rounded.Tune, null) },
                            enabled = running, onClick = { menu = false; dialog = Dlg.DEVICE })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_adb)) }, leadingIcon = { Icon(Icons.Rounded.Lan, null) },
                            enabled = running, onClick = { menu = false; dialog = Dlg.ADB })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_settings)) }, leadingIcon = { Icon(Icons.Rounded.Settings, null) },
                            enabled = running, onClick = { menu = false; guestAsync("am start -a android.settings.SETTINGS") })
                        DropdownMenuItem(text = { Text(stringResource(R.string.m_apps)) }, leadingIcon = { Icon(Icons.Rounded.Apps, null) },
                            enabled = running, onClick = { menu = false; dialog = Dlg.APPS })
                    }
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_screenshot)) }, leadingIcon = { Icon(Icons.Rounded.Screenshot, null) },
                        enabled = state == GuestVm.State.RUNNING, onClick = { menu = false; screenshot() })
                    DropdownMenuItem(text = { Text(stringResource(if (recOn.value) R.string.m_record_stop else R.string.m_record)) },
                        leadingIcon = { Icon(Icons.Rounded.Videocam, null) },
                        enabled = state == GuestVm.State.RUNNING || recOn.value, onClick = { menu = false; toggleRecording() })
                    DropdownMenuItem(text = { Text(stringResource(if (vncOn.value) R.string.m_vnc_stop else R.string.m_vnc)) },
                        leadingIcon = { Icon(Icons.Rounded.Cast, null) },
                        onClick = { menu = false; toggleVnc() })
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_reset_menu_pos)) }, leadingIcon = { Icon(Icons.Rounded.RestartAlt, null) },
                        onClick = { menu = false; menuOffset = Offset.Zero; saveMenuOffset() })
                    HorizontalDivider()
                    DropdownMenuItem(text = { Text(stringResource(R.string.m_reboot)) }, leadingIcon = { Icon(Icons.Rounded.RestartAlt, null) },
                        onClick = { menu = false; rebootVm() })
                    DropdownMenuItem(text = { Text(stringResource(R.string.shutdown)) }, leadingIcon = { Icon(Icons.Rounded.Close, null) },
                        onClick = { menu = false; stopVm() })
                }
            }

            when (dialog) {
                Dlg.SMS -> SmsDialog { dialog = Dlg.NONE }
                Dlg.CALL -> CallDialog { dialog = Dlg.NONE }
                Dlg.BATTERY -> BatteryDialog { dialog = Dlg.NONE }
                Dlg.ADB -> AdbDialog { dialog = Dlg.NONE }
                Dlg.DEVICE -> DeviceDialog { dialog = Dlg.NONE }
                Dlg.CAMERA -> CameraSettings(this@VmActivity, vm.paths, vm.img.api) { dialog = Dlg.NONE }
                Dlg.APPS -> AppsDialog { dialog = Dlg.NONE }
                Dlg.BADGE -> BadgeDialog { dialog = Dlg.NONE }
                Dlg.HUD -> HudDialog { dialog = Dlg.NONE }
                Dlg.NONE -> {}
            }

            // pre-1.0 builds (Sooner/M3/M5) target phones driven by keys: their own keypad instead of the nav bar
            if (hasDeviceKeys && !showLog && !showTerm) {
                DeviceKeypad(Modifier.align(Alignment.BottomCenter).navigationBarsPadding().padding(bottom = 4.dp))
            } else if (vm.settings.showNavBar && !showLog && !showTerm) {
                val pad = if (vm.settings.navSize == 0) 3.dp else 6.dp
                if (androidx.compose.ui.platform.LocalConfiguration.current.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE)
                    NavBar(Modifier.align(Alignment.CenterEnd).navigationBarsPadding().displayCutoutPadding().padding(end = pad), vertical = true)
                else NavBar(Modifier.align(Alignment.BottomCenter).navigationBarsPadding().padding(bottom = pad))
            }
        }
    }

    /** Builds before API 3 select with the D-pad (a tap only moves focus) and have Call/End and a QWERTY keyboard. */
    private val hasDeviceKeys: Boolean get() = vm.img.api in 1..2

    /** A guest key held down for as long as the finger stays on it. */
    @Composable
    private fun PadKey(label: String, code: Int, modifier: Modifier = Modifier, w: androidx.compose.ui.unit.Dp = 44.dp,
                       h: androidx.compose.ui.unit.Dp = 40.dp) {
        var pressed by remember { mutableStateOf(false) }
        Surface(shape = MaterialTheme.shapes.small,
            color = if (pressed) MaterialTheme.colorScheme.primary.copy(alpha = 0.5f) else MaterialTheme.colorScheme.surfaceContainerHighest,
            modifier = modifier.size(w, h).pointerInput(code) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    pressed = true
                    vm.input.key(code, true)
                    while (true) {
                        val c = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                        c.consume()
                        if (!c.pressed) break
                    }
                    pressed = false
                    vm.input.key(code, false)
                }
            }) {
            Box(contentAlignment = Alignment.Center) { Text(label, style = MaterialTheme.typography.labelLarge, maxLines = 1) }
        }
    }

    /** Keypad of a pre-1.0 phone: Call/Home/Menu/Back/End, a D-pad with its center and a QWERTY that slides out. */
    @Composable
    private fun DeviceKeypad(modifier: Modifier) {
        var qwerty by remember { mutableStateOf(false) }
        Surface(shape = MaterialTheme.shapes.large, color = MaterialTheme.colorScheme.surfaceContainerHigh.copy(alpha = 0.92f), modifier = modifier) {
            Column(Modifier.padding(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                if (qwerty) {
                    val rows = listOf(
                        "qwertyuiop".toList().zip(listOf(16, 17, 18, 19, 20, 21, 22, 23, 24, 25)),
                        "asdfghjkl".toList().zip(listOf(30, 31, 32, 33, 34, 35, 36, 37, 38)),
                        "zxcvbnm".toList().zip(listOf(44, 45, 46, 47, 48, 49, 50)),
                    )
                    for (r in rows) Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                        for ((ch, sc) in r) PadKey(ch.toString(), sc, w = 32.dp, h = 38.dp)
                    }
                    Row(horizontalArrangement = Arrangement.spacedBy(3.dp)) {
                        PadKey("⇧", 42, w = 44.dp, h = 38.dp); PadKey("Alt", 56, w = 44.dp, h = 38.dp)
                        PadKey("", 57, w = 120.dp, h = 38.dp); PadKey(".", 52, w = 32.dp, h = 38.dp)
                        PadKey("⌫", 14, w = 44.dp, h = 38.dp); PadKey("↵", 28, w = 44.dp, h = 38.dp)
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        PadKey("Call", 231, w = 56.dp); PadKey("Menu", InputService.KEY_MENU, w = 56.dp)
                    }
                    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        PadKey("▲", InputService.KEY_UP, h = 32.dp)
                        Row(horizontalArrangement = Arrangement.spacedBy(2.dp)) {
                            PadKey("◀", InputService.KEY_LEFT, h = 32.dp); PadKey("●", InputService.KEY_CENTER, h = 32.dp)
                            PadKey("▶", InputService.KEY_RIGHT, h = 32.dp)
                        }
                        PadKey("▼", InputService.KEY_DOWN, h = 32.dp)
                    }
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        PadKey("End", 107, w = 56.dp); PadKey("Back", InputService.KEY_BACK, w = 56.dp)
                    }
                    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        PadKey("Home", InputService.KEY_HOME, w = 56.dp)
                        Surface(shape = MaterialTheme.shapes.small, color = if (qwerty) MaterialTheme.colorScheme.primary.copy(alpha = 0.5f)
                            else MaterialTheme.colorScheme.surfaceContainerHighest,
                            onClick = { qwerty = !qwerty }, modifier = Modifier.size(56.dp, 40.dp)) {
                            Box(contentAlignment = Alignment.Center) { Icon(Icons.Rounded.Keyboard, stringResource(R.string.m_keyboard)) }
                        }
                    }
                }
            }
        }
    }

    /** The bar of guest buttons: the user's own set, each held down in the guest for as long as it is held here. */
    @Composable
    private fun NavBar(modifier: Modifier, vertical: Boolean = false) {
        val compact = vm.settings.navSize == 0
        val keys = remember { vm.settings.navKeys.split(',').filter { it in app.aemu.core.VmSettings.NAV_ALL } }
        if (keys.isEmpty()) return
        Surface(shape = CircleShape, color = MaterialTheme.colorScheme.surfaceContainerHigh.copy(alpha = 0.92f), modifier = modifier) {
            val gap = if (compact) 6.dp else 4.dp
            val along = if (compact) 6.dp else 10.dp
            val across = if (compact) 2.dp else 6.dp
            if (vertical) Column(Modifier.padding(horizontal = across, vertical = along), verticalArrangement = Arrangement.spacedBy(gap)) {
                for (k in keys) NavKey(k, if (compact) 36.dp else 48.dp)
            } else Row(Modifier.padding(horizontal = along, vertical = across), horizontalArrangement = Arrangement.spacedBy(gap)) {
                for (k in keys) NavKey(k, if (compact) 36.dp else 48.dp)
            }
        }
    }

    @Composable
    private fun NavKey(k: String, size: androidx.compose.ui.unit.Dp) {
        var pressed by remember { mutableStateOf(false) }
        val bg by androidx.compose.animation.animateColorAsState(
            if (pressed) MaterialTheme.colorScheme.primary.copy(alpha = 0.28f) else Color.Transparent, label = "key")
        val code = when (k) {
            "back" -> InputService.KEY_BACK; "home" -> InputService.KEY_HOME; "menu" -> InputService.KEY_MENU
            "search" -> InputService.KEY_SEARCH; "volup" -> InputService.KEY_VOLUMEUP; "voldown" -> InputService.KEY_VOLUMEDOWN
            "power" -> InputService.KEY_POWER; else -> 0
        }
        val step = with(androidx.compose.ui.platform.LocalDensity.current) { 18.dp.toPx() }
        Box(Modifier.size(size).background(bg, CircleShape).pointerInput(k) {
            awaitEachGesture {
                val down = awaitFirstDown()
                pressed = true
                val t0 = android.os.SystemClock.uptimeMillis()
                if (k == "home") vm.input.home(true)
                else if (code != 0) vm.input.key(code, true)
                // the D-pad button is a little trackball: sliding the finger sends arrows, a tap selects
                var acc = Offset.Zero
                var moved = false
                while (true) {
                    val e = awaitPointerEvent()
                    val c = e.changes.firstOrNull { it.id == down.id } ?: break
                    if (k == "dpad") {
                        acc += c.position - c.previousPosition
                        while (kotlin.math.abs(acc.x) >= step || kotlin.math.abs(acc.y) >= step) {
                            moved = true
                            if (kotlin.math.abs(acc.x) >= kotlin.math.abs(acc.y)) {
                                vm.input.press(if (acc.x > 0) InputService.KEY_RIGHT else InputService.KEY_LEFT, 30)
                                acc = Offset(acc.x - step * kotlin.math.sign(acc.x), acc.y)
                            } else {
                                vm.input.press(if (acc.y > 0) InputService.KEY_DOWN else InputService.KEY_UP, 30)
                                acc = Offset(acc.x, acc.y - step * kotlin.math.sign(acc.y))
                            }
                        }
                    }
                    c.consume()
                    if (!c.pressed) break
                }
                pressed = false
                if (k == "home") vm.input.home(false)
                else if (code != 0) vm.input.key(code, false)
                else if (k == "recents") vm.input.recents(vm.img.api, vm.img.skin)
                else if (k == "dpad" && !moved) vm.input.press(InputService.KEY_CENTER, if (android.os.SystemClock.uptimeMillis() - t0 > 500) 700 else 80)
            }
        }, contentAlignment = Alignment.Center) {
            val d = navKeyLabel(k)
            val m = Modifier.size(if (size < 40.dp) 20.dp else 24.dp)
            when (k) {
                "back" -> Icon(Icons.AutoMirrored.Rounded.ArrowBack, d, m)
                "home" -> Icon(Icons.Rounded.Circle, d, m)
                "recents" -> Icon(Icons.Rounded.CropSquare, d, m)
                "menu" -> Icon(Icons.Rounded.Menu, d, m)
                "search" -> Icon(Icons.Rounded.Search, d, m)
                "volup" -> Icon(Icons.Rounded.VolumeUp, d, m)
                "voldown" -> Icon(Icons.Rounded.VolumeDown, d, m)
                "power" -> Icon(Icons.Rounded.PowerSettingsNew, d, m)
                else -> Icon(Icons.Rounded.Gamepad, d, m)
            }
        }
    }

    /**
     * The guest's LED above its screen: Sony's illumination bar as a glowing strip, other phones' notification LED as
     * a dot. Blinks with the guest's on/off times.
     */
    @Composable
    private fun LedIndicator(l: app.aemu.core.Lights.Led, modifier: Modifier) {
        var on by remember(l) { mutableStateOf(true) }
        LaunchedEffect(l) {
            if (l.onMs > 0 && l.offMs > 0) while (true) { on = true; delay(l.onMs.toLong()); on = false; delay(l.offMs.toLong()) }
        }
        val alpha by androidx.compose.animation.core.animateFloatAsState(if (on) 1f else 0.08f,
            androidx.compose.animation.core.tween(if (on) 120 else 260), label = "led")
        val c = Color(0xff000000.toInt() or (l.color and 0xffffff))
        val sony = vm.img.skin.contains("Xperia", true) || vm.img.brand.contains("Sony", true)
        val density = androidx.compose.ui.platform.LocalDensity.current
        val y = with(density) { (guestTopPx.toDp() - 10.dp).coerceAtLeast(4.dp) }
        if (sony) {
            Box(
                modifier.offset(y = y)
                    .width(160.dp)
                    .height(6.dp)
                    .alpha(alpha)
                    .clip(RoundedCornerShape(3.dp))
                    .background(
                        androidx.compose.ui.graphics.Brush.horizontalGradient(
                            listOf(
                                Color.Transparent,
                                c.copy(alpha = 0.4f),
                                c,
                                c.copy(alpha = 0.4f),
                                Color.Transparent
                            )
                        )
                    )
            )
        } else {
            Box(
                modifier.offset(y = y).size(14.dp)
                    .alpha(alpha)
                    .background(androidx.compose.ui.graphics.Brush.radialGradient(listOf(c, c.copy(alpha = 0.55f), Color.Transparent)), CircleShape)
            )
        }
    }

    @Composable
    private fun BootCard(seconds: Long, unconfirmed: Boolean, onContinueWaiting: () -> Unit) {
        Card(
            modifier = Modifier.widthIn(max = 360.dp).padding(24.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh.copy(alpha = 0.94f),
                contentColor = MaterialTheme.colorScheme.onSurface),
            shape = MaterialTheme.shapes.extraLarge,
        ) {
            Column(Modifier.padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                if (stopError != null) {
                    Text(stringResource(R.string.stop_failed), style = MaterialTheme.typography.headlineSmall)
                    Spacer(Modifier.height(8.dp))
                    Text(stopError ?: "", color = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.height(16.dp))
                    Button(enabled = !stopPending, onClick = { stopVm() }) { Text(stringResource(R.string.retry)) }
                } else if (poweredOff) {
                    Text(stringResource(R.string.powered_off), style = MaterialTheme.typography.headlineSmall)
                    Spacer(Modifier.height(16.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = { stopVm() }) { Text(stringResource(R.string.close)) }
                        OutlinedButton(onClick = { openLog = true }) { Text(stringResource(R.string.log)) }
                        Button(enabled = allowPowerOffRetry(stopPending), onClick = { rebootVm() }) { Text(stringResource(R.string.retry)) }
                    }
                } else if (state == GuestVm.State.FAILED || previousBootMessage != null) {
                    Text(stringResource(R.string.failed_start), style = MaterialTheme.typography.headlineSmall)
                    Spacer(Modifier.height(8.dp))
                    Text(previousBootMessage ?: vm.failure ?: "", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.height(16.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = { stopVm() }) { Text(stringResource(R.string.close)) }
                        OutlinedButton(onClick = { openLog = true }) { Text(stringResource(R.string.log)) }
                        Button(enabled = !retryPending, onClick = {
                            retryPending = true
                            thread {
                                try { vm.stop(); startBoot() }
                                catch (e: Exception) {
                                    runOnUiThread {
                                        previousBootMessage = e.message ?: getString(R.string.failed_start)
                                        retryPending = false
                                    }
                                }
                            }
                        }) { Text(stringResource(R.string.retry)) }
                    }
                } else if (unconfirmed) {
                    Text(stringResource(R.string.boot_unconfirmed_title), style = MaterialTheme.typography.headlineSmall)
                    Spacer(Modifier.height(8.dp))
                    Text(stringResource(R.string.boot_unconfirmed_message), style = MaterialTheme.typography.bodyMedium)
                    Spacer(Modifier.height(16.dp))
                    Button(onClick = onContinueWaiting) { Text(stringResource(R.string.boot_continue_waiting)) }
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        OutlinedButton(onClick = { stopVm() }) { Text(stringResource(R.string.boot_stop)) }
                        OutlinedButton(enabled = !retryPending, onClick = {
                            retryPending = true
                            thread {
                                try { vm.stop(); startBoot() }
                                catch (e: Exception) {
                                    runOnUiThread {
                                        previousBootMessage = e.message ?: getString(R.string.failed_start)
                                        retryPending = false
                                    }
                                }
                            }
                        }) { Text(stringResource(R.string.retry)) }
                    }
                } else {
                    LoadingIndicator(Modifier.size(64.dp))
                    Spacer(Modifier.height(16.dp))
                    Text(vm.img.name, style = MaterialTheme.typography.titleLarge, maxLines = 2, overflow = TextOverflow.Ellipsis)
                    Text(vm.img.displayVersion, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.height(12.dp))
                    val phase = when (state) {
                        GuestVm.State.PREPARING -> stringResource(R.string.st_preparing)
                        GuestVm.State.BOOTING -> stringResource(R.string.st_booting, seconds.toInt())
                        GuestVm.State.STOPPING -> stringResource(R.string.st_stopping)
                        else -> "…"
                    }
                    Text(phase, style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
                    if (vm.img.bootCount == 0 && state == GuestVm.State.BOOTING) {
                        Spacer(Modifier.height(6.dp))
                        Text(stringResource(R.string.first_boot_hint),
                            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(logLines.lastOrNull()?.substringAfter(' ') ?: "", style = Mono, maxLines = 2,
                        overflow = TextOverflow.Ellipsis, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
    }

    @Composable
    private fun LogPanel(onClose: () -> Unit) {
        var guestTab by remember { mutableStateOf(false) }
        val guestLines = remember { mutableStateListOf<String>() }
        LaunchedEffect(guestTab) {
            while (guestTab) {
                val recs = GuestLog.tail(vm.paths.root, 128 * 1024).takeLast(300)
                guestLines.clear(); guestLines.addAll(recs.map { "${it.prio} ${it.tag}: ${it.msg}" })
                delay(1500)
            }
        }
        val list = if (guestTab) guestLines else logLines
        Surface(Modifier.fillMaxSize(), color = Color(0xF0101410), contentColor = Color(0xFFE2E3DD)) {
            Column(Modifier.statusBarsPadding().navigationBarsPadding().imePadding().padding(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.log), style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f),
                        maxLines = 1, overflow = TextOverflow.Ellipsis)
                    IconButton(onClick = { copyLog(list.toList()) }) { Icon(Icons.Rounded.ContentCopy, stringResource(R.string.log_copy)) }
                    IconButton(onClick = { pendingExport = list.toList(); exportLog.launch("RedMagicEMU-${vm.img.id}-${if (guestTab) "logcat" else "host"}.log") }) {
                        Icon(Icons.Rounded.Download, stringResource(R.string.log_export))
                    }
                    FilledTonalIconButton(onClick = onClose) { Icon(Icons.Rounded.Close, stringResource(R.string.close)) }
                }
                Spacer(Modifier.height(8.dp))
                SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
                    SegmentedButton(selected = !guestTab, onClick = { guestTab = false }, shape = SegmentedButtonDefaults.itemShape(0, 2)) {
                        Text(stringResource(R.string.host), maxLines = 1)
                    }
                    SegmentedButton(selected = guestTab, onClick = { guestTab = true }, shape = SegmentedButtonDefaults.itemShape(1, 2)) {
                        Text(stringResource(R.string.guest_log), maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
                Spacer(Modifier.height(8.dp))
                val st = rememberLazyListState()
                LaunchedEffect(list.size) { if (list.isNotEmpty()) st.scrollToItem(list.size - 1) }
                SelectionContainer {
                    LazyColumn(state = st, modifier = Modifier.fillMaxSize()) {
                        items(list) { Text(it, style = Mono, color = Color(0xFFCFE8CF)) }
                    }
                }
            }
        }
    }

    // shell terminal into the guest: its lines survive closing and reopening the panel
    private val termLines = mutableStateListOf<String>()
    private val termHistory = ArrayList<String>()

    @Composable
    private fun TerminalPanel(onClose: () -> Unit) {
        var cmd by remember { mutableStateOf("") }
        var busy by remember { mutableStateOf(false) }
        var histPos by remember { mutableStateOf(-1) }
        fun run() {
            val c = cmd.trim()
            if (c.isEmpty() || busy) return
            cmd = ""; histPos = -1
            termHistory.remove(c); termHistory.add(c)
            if (c == "clear") { termLines.clear(); return }
            termLines.add("$ $c")
            busy = true
            thread {
                val out = runCatching { vm.guestShell(c, 180_000) }.getOrElse { it.toString() }
                runOnUiThread {
                    termLines.addAll(out.trimEnd('\n').lines())
                    while (termLines.size > 3000) termLines.removeAt(0)
                    busy = false
                }
            }
        }
        Surface(Modifier.fillMaxSize(), color = Color(0xF0101410), contentColor = Color(0xFFE2E3DD)) {
            Column(Modifier.statusBarsPadding().navigationBarsPadding().imePadding().padding(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.m_terminal), style = MaterialTheme.typography.titleLarge, modifier = Modifier.weight(1f),
                        maxLines = 1, overflow = TextOverflow.Ellipsis)
                    if (busy) LoadingIndicator(Modifier.size(32.dp))
                    IconButton(onClick = { copyLog(termLines.toList()) }) { Icon(Icons.Rounded.ContentCopy, stringResource(R.string.log_copy)) }
                    FilledTonalIconButton(onClick = onClose) { Icon(Icons.Rounded.Close, stringResource(R.string.close)) }
                }
                Spacer(Modifier.height(8.dp))
                val st = rememberLazyListState()
                LaunchedEffect(termLines.size) { if (termLines.isNotEmpty()) st.scrollToItem(termLines.size - 1) }
                SelectionContainer(Modifier.weight(1f)) {
                    LazyColumn(state = st, modifier = Modifier.fillMaxSize()) {
                        items(termLines) { Text(it, style = Mono, color = if (it.startsWith("$ ")) Color(0xFF9AD0FF) else Color(0xFFCFE8CF)) }
                    }
                }
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = {
                        if (termHistory.isNotEmpty()) {
                            histPos = if (histPos <= 0) termHistory.size - 1 else histPos - 1
                            cmd = termHistory[histPos]
                        }
                    }) { Icon(Icons.Rounded.History, null) }
                    OutlinedTextField(cmd, { cmd = it }, modifier = Modifier.weight(1f), singleLine = true,
                        textStyle = Mono, placeholder = { Text("getprop ro.build.version.release", style = Mono) },
                        keyboardOptions = androidx.compose.foundation.text.KeyboardOptions(imeAction = androidx.compose.ui.text.input.ImeAction.Send,
                            autoCorrectEnabled = false, capitalization = androidx.compose.ui.text.input.KeyboardCapitalization.None),
                        keyboardActions = androidx.compose.foundation.text.KeyboardActions(onSend = { run() }))
                    Spacer(Modifier.width(8.dp))
                    FilledTonalIconButton(onClick = { run() }, enabled = !busy && cmd.isNotBlank()) {
                        Icon(Icons.AutoMirrored.Rounded.Send, stringResource(R.string.m_send))
                    }
                }
            }
        }
    }

    private var pendingExport: List<String> = emptyList()
    private val exportLog = registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri ->
        if (uri == null) return@registerForActivityResult
        val lines = pendingExport
        thread {
            val ok = runCatching { contentResolver.openOutputStream(uri)!!.bufferedWriter().use { w -> lines.forEach { w.write(it); w.write("\n") } } }.isSuccess
            runOnUiThread { Toast.makeText(this, if (ok) R.string.log_exported else R.string.log_export_failed, Toast.LENGTH_SHORT).show() }
        }
    }

    private fun copyLog(lines: List<String>) {
        val cm = getSystemService(ClipboardManager::class.java)
        cm.setPrimaryClip(ClipData.newPlainText("RedMagicEMU log", lines.joinToString("\n")))
        Toast.makeText(this, R.string.log_copied, Toast.LENGTH_SHORT).show()
    }

    companion object {
        const val EXTRA_ID = "id"
        const val EXTRA_RECOVERY = "recovery"
        fun start(ctx: Context, id: String, recovery: Boolean = false) {
            app.aemu.AppPrefs.setLastRun(ctx, id, System.currentTimeMillis())
            ctx.startActivity(Intent(ctx, VmActivity::class.java).putExtra(EXTRA_ID, id).putExtra(EXTRA_RECOVERY, recovery)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        }
    }
}

internal fun shouldBootOnAttach(state: GuestVm.State, previousFailure: Boolean = false, poweredOff: Boolean = false): Boolean =
    state == GuestVm.State.STOPPED && !previousFailure && !poweredOff

internal fun shouldShowBootNotice(state: GuestVm.State): Boolean =
    state == GuestVm.State.BOOTING
