package app.aemu.ui

import android.graphics.BitmapFactory
import androidx.annotation.StringRes
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.LinearWavyProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import app.aemu.R
import app.aemu.core.InstancePathSafety
import app.aemu.core.VmPaths
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.DataInputStream
import java.io.File
import java.io.IOException
import java.text.NumberFormat
import java.util.zip.ZipFile

private const val THEME_PATH = "root/system/etc/custom_config/themes/IP_MC_W_CN/default_theme_64.zmtp"
private const val WALLPAPER_ENTRY = "lockscreen/wallpaper1.jpg"
private const val MAX_WALLPAPER_BYTES = 2 * 1024 * 1024L
private const val DISPLAY_WIDTH = 1216
private const val DISPLAY_HEIGHT = 2688

@Composable
internal fun HomeDevice(
    instanceId: String?,
    @StringRes status: Int?,
    phoneHeight: Dp,
    screen: (@Composable () -> Unit)? = null,
    actions: @Composable () -> Unit,
) {
    val ctx = LocalContext.current
    val wallpaper by produceState<ImageBitmap?>(null, instanceId) {
        value = null
        if (instanceId != null) {
            value = withContext(Dispatchers.IO) {
                val bytes = readHomeWallpaper(VmPaths(ctx, instanceId).dir) ?: return@withContext null
                val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options)
                if (options.outWidth != DISPLAY_WIDTH || options.outHeight != DISPLAY_HEIGHT) return@withContext null
                options.inJustDecodeBounds = false
                options.inSampleSize = 2
                BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options)?.asImageBitmap()
            }
        }
    }
    Column(
        Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        PhoneFront(phoneHeight, wallpaper, screen)
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                stringResource(R.string.home_device_model),
                style = MaterialTheme.typography.titleLarge,
                textAlign = TextAlign.Center,
            )
            val version = stringResource(R.string.home_android_version)
            val versionAndStatus = if (status == null) version else "$version · ${stringResource(status)}"
            Text(
                versionAndStatus,
                style = MaterialTheme.typography.bodySmall,
                fontSize = 13.sp,
                fontWeight = FontWeight.Light,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
        }
        actions()
    }
}

@Composable
private fun PhoneFront(height: Dp, wallpaper: ImageBitmap?, screen: (@Composable () -> Unit)?) {
    val frame = RoundedCornerShape(12.dp)
    val rim = 2.dp
    val bezel = 3.dp
    val totalInset = (rim + bezel) * 2
    val width = (height - totalInset) * (DISPLAY_WIDTH.toFloat() / DISPLAY_HEIGHT) + totalInset
    Box(
        Modifier
            .height(height)
            .width(width)
            .shadow(10.dp, frame)
            .background(Brush.linearGradient(listOf(Color(0xFF77727A), Color(0xFF201D22), Color(0xFF50303A))), frame)
            .border(1.dp, Color(0xFF373139), frame)
            .padding(rim)
            .background(Color(0xFF09080B), RoundedCornerShape(10.dp))
            .padding(bezel)
            .clip(RoundedCornerShape(7.dp))
            .background(Brush.verticalGradient(listOf(Color(0xFF2B222B), Color(0xFF121015)))),
    ) {
        if (wallpaper != null) {
            Image(wallpaper, contentDescription = null, modifier = Modifier.fillMaxSize())
        }
        screen?.invoke()
    }
}

@Composable
internal fun HomeImportProgress(progress: Float, @StringRes step: Int) {
    BoxWithConstraints(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        val compact = maxWidth < 112.dp || maxHeight < 190.dp
        Column(
            modifier = Modifier.fillMaxWidth().padding(horizontal = if (compact) 7.dp else 15.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(if (compact) 4.dp else 8.dp),
        ) {
            if (!compact) {
                Text(
                    stringResource(R.string.import_running),
                    style = MaterialTheme.typography.titleSmall,
                    color = Color(0xFFF0EDF7),
                    textAlign = TextAlign.Center,
                )
            }
            if (progress in 0f..1f) {
                val animated by animateFloatAsState(progress, label = "firmware import")
                val locale = LocalConfiguration.current.locales[0]
                val formatter = remember(locale) { NumberFormat.getPercentInstance(locale) }
                Text(
                    formatter.format(progress),
                    style = if (compact) MaterialTheme.typography.titleMedium else MaterialTheme.typography.headlineMedium,
                    color = MaterialTheme.colorScheme.primary,
                )
                LinearWavyProgressIndicator(progress = { animated.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth())
            } else {
                LinearWavyProgressIndicator(modifier = Modifier.fillMaxWidth())
            }
            Text(
                stringResource(step),
                style = MaterialTheme.typography.bodySmall,
                color = Color(0xFFCCC4D2),
                textAlign = TextAlign.Center,
                maxLines = 2,
            )
        }
    }
}

@StringRes internal fun importScreenStep(step: String): Int = when (step) {
    "Staging firmware" -> R.string.import_stage_copy
    "Checking firmware package" -> R.string.import_stage_check
    "Unpacking firmware images" -> R.string.import_stage_unpack
    "Extracting firmware" -> R.string.import_stage_extract
    "Checking firmware fingerprint" -> R.string.import_stage_fingerprint
    "Analyzing firmware" -> R.string.import_stage_analyze
    else -> R.string.import_start
}

internal fun readHomeWallpaper(instance: File): ByteArray? {
    val theme = File(instance, THEME_PATH)
    if (!InstancePathSafety.safeRelative(instance, THEME_PATH) || !theme.isFile) return null
    return try {
        ZipFile(theme).use { zip ->
            val entry = zip.getEntry(WALLPAPER_ENTRY) ?: return null
            if (entry.size !in 1..MAX_WALLPAPER_BYTES) return null
            ByteArray(entry.size.toInt()).also { bytes ->
                DataInputStream(zip.getInputStream(entry)).use { it.readFully(bytes) }
            }
        }
    } catch (_: IOException) {
        null
    }
}
