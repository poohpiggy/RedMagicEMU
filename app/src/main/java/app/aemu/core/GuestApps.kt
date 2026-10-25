package app.aemu.core

import android.content.Context
import android.graphics.Bitmap
import androidx.core.graphics.drawable.toBitmap
import java.io.File

/** One launchable app of the firmware, read on the host from its APK inside the image. */
data class GuestApp(val pkg: String, val label: String, val version: String, val icon: Bitmap?, val system: Boolean)

object GuestApps {
    @Volatile var lastError = ""
    /** packages with a MAIN activity, from "dumpsys package r" (2.3+; "cmd package query-activities" is 7.0+) */
    fun parseMain(dump: String): List<String> {
        val list = sortedSetOf<String>()
        var inMain = false
        val comp = Regex("""\s([\w.]+)/""")
        for (l in dump.lineSequence()) {
            val t = l.trimEnd()
            if (t.endsWith(":") && !t.trimStart().contains(' ')) inMain = t.trim() == "android.intent.action.MAIN:"
            else if (inMain) comp.find(t)?.let { list.add(it.groupValues[1]) }
        }
        return list.toList()
    }

    /** "package:/system/app/Browser.apk=com.android.browser" lines from "pm list packages -f" */
    fun parsePaths(out: String): Map<String, String> = out.lineSequence().mapNotNull { l ->
        val t = l.trim().removePrefix("package:")
        val i = t.lastIndexOf('=')
        if (i <= 0) null else t.substring(i + 1) to t.substring(0, i)
    }.toMap()

    /** label, version and icon from the APK in the image's tree; the package name stands in for what cannot be read */
    fun load(ctx: Context, root: File, pkg: String, guestPath: String?): GuestApp {
        val system = guestPath == null || !guestPath.startsWith("/data/")
        val f = guestPath?.let { File(root, it.removePrefix("/")) }
        if (f == null || !f.isFile) { lastError = "no file $f"; return GuestApp(pkg, pkg, "", null, system) }
        return runCatching {
            val pm = ctx.packageManager
            val pi = pm.getPackageArchiveInfo(f.absolutePath, 0)
            if (pi == null) { lastError = "unparsed $f"; return GuestApp(pkg, pkg, "", null, system) }
            val ai = pi.applicationInfo ?: return GuestApp(pkg, pkg, pi.versionName.orEmpty(), null, system)
            ai.sourceDir = f.absolutePath; ai.publicSourceDir = f.absolutePath
            val label = runCatching { ai.loadLabel(pm).toString() }.getOrNull()?.takeIf { it.isNotBlank() && it != pkg } ?: pkg
            val icon = runCatching { ai.loadIcon(pm).toBitmap(96, 96) }.getOrNull()
            GuestApp(pkg, label, pi.versionName.orEmpty(), icon, system)
        }.getOrElse { lastError = it.toString(); GuestApp(pkg, pkg, "", null, system) }
    }
}
