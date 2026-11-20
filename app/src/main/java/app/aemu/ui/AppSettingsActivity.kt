package app.aemu.ui

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Code
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.Favorite
import androidx.compose.material.icons.rounded.History
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.Person
import androidx.compose.material.icons.rounded.Public
import androidx.compose.material.icons.rounded.Send
import androidx.compose.material.icons.rounded.SystemUpdate
import androidx.compose.material.icons.rounded.Translate
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ButtonGroupDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.ToggleButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import app.aemu.AppPrefs
import app.aemu.BuildConfig
import app.aemu.R

class AppSettingsActivity : ComponentActivity() {
    override fun attachBaseContext(base: Context) = super.attachBaseContext(AppPrefs.wrap(base))

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent { AemuTheme { AppSettings(onBack = ::finish, onRestyle = ::restart) } }
    }

    /** Язык применяется пересозданием активности для применения нового ContextWrapper с локалью. */
    private fun restart() {
        recreate()
    }
}

@Composable
private fun AppSettings(onBack: () -> Unit, onRestyle: () -> Unit) {
    val ctx = LocalContext.current
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()
    var langDialog by remember { mutableStateOf(false) }
    var theme by remember { mutableStateOf(AppPrefs.theme(ctx)) }
    var dynamic by remember { mutableStateOf(AppPrefs.dynamicColor(ctx)) }
    val langTag = AppPrefs.language(ctx)
    val langName = AppPrefs.LANGUAGES.firstOrNull { it.first == langTag }?.second?.ifEmpty { null } ?: stringResource(R.string.as_lang_system)

    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.about_title)) },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Rounded.ArrowBack, stringResource(R.string.back)) } },
                scrollBehavior = scroll,
            )
        },
    ) { pad ->
        LazyColumn(
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = pad.calculateTopPadding() + 8.dp, bottom = pad.calculateBottomPadding() + 32.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            item { AboutCard() }

            item {
                Section(stringResource(R.string.as_appearance)) {
                    Row_(Icons.Rounded.Translate, stringResource(R.string.as_language), langName) { langDialog = true }
                    Text(stringResource(R.string.as_theme), style = MaterialTheme.typography.labelLarge,
                        modifier = Modifier.padding(start = 16.dp, top = 8.dp, bottom = 8.dp))
                    val options = listOf(R.string.as_theme_system, R.string.as_theme_light, R.string.as_theme_dark)
                    Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(ButtonGroupDefaults.ConnectedSpaceBetween)) {
                        options.forEachIndexed { i, res ->
                            ToggleButton(
                                checked = theme == i,
                                onCheckedChange = { if (theme != i) { theme = i; AppPrefs.setTheme(ctx, i) } },
                                shapes = when (i) {
                                    0 -> ButtonGroupDefaults.connectedLeadingButtonShapes()
                                    options.lastIndex -> ButtonGroupDefaults.connectedTrailingButtonShapes()
                                    else -> ButtonGroupDefaults.connectedMiddleButtonShapes()
                                },
                                modifier = Modifier.weight(1f).semantics { role = Role.RadioButton },
                                contentPadding = PaddingValues(horizontal = 8.dp),
                            ) {
                                // одна строка: длинные названия («Системная») не переносятся по буквам
                                Text(stringResource(res), maxLines = 1, softWrap = false,
                                    overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis, style = MaterialTheme.typography.labelLarge)
                            }
                        }
                    }
                    if (Build.VERSION.SDK_INT >= 31) {
                        Toggle(stringResource(R.string.as_dynamic), stringResource(R.string.as_dynamic_sub), dynamic) {
                            dynamic = it; AppPrefs.setDynamicColor(ctx, it)
                        }
                    }
                }
            }

            item {
                Section(stringResource(R.string.as_links)) {
                    AboutLinkRow(
                        icon = Icons.Rounded.Public,
                        title = stringResource(R.string.as_project_address),
                        subtitle = "github.com/poohpiggy/RedMagicEMU",
                        path = "poohpiggy/RedMagicEMU",
                    )
                    AboutLinkRow(
                        icon = Icons.Rounded.Person,
                        title = stringResource(R.string.as_developer),
                        subtitle = "poohpiggy",
                        path = "poohpiggy",
                        avatar = R.drawable.about_avatar_poohpiggy,
                    )
                }
            }

            item {
                Section(stringResource(R.string.as_acknowledgments)) {
                    AboutLinkRow(
                        icon = Icons.Rounded.Code,
                        title = "AEmulator",
                        subtitle = stringResource(R.string.as_upstream_credit, "uxazu"),
                        path = "uxazu/AEmulator",
                        author = "uxazu",
                        avatar = R.drawable.about_avatar_uxazu,
                    )
                    AboutLinkRow(
                        icon = Icons.Rounded.Code,
                        title = "SENSES",
                        subtitle = stringResource(R.string.as_original_credit, "Istratii-tech"),
                        path = "Istratii-tech/SENSES",
                        author = "Istratii-tech",
                        avatar = R.drawable.about_avatar_istratii_tech,
                    )
                }
            }

        }
    }

    if (langDialog) {
        AlertDialog(
            onDismissRequest = { langDialog = false },
            icon = { Icon(Icons.Rounded.Language, null) },
            title = { Text(stringResource(R.string.as_language)) },
            text = {
                LazyColumn {
                    items(AppPrefs.LANGUAGES) { (tag, name) ->
                        Row(
                            Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).clickable {
                                langDialog = false
                                if (tag != langTag) { AppPrefs.setLanguage(ctx, tag); onRestyle() }
                            }.padding(vertical = 4.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            RadioButton(selected = tag == langTag, onClick = null, modifier = Modifier.padding(horizontal = 12.dp))
                            Text(name.ifEmpty { stringResource(R.string.as_lang_system) }, style = MaterialTheme.typography.bodyLarge)
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { langDialog = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }


}

@Composable
private fun AboutCard() {
    Card(shape = MaterialTheme.shapes.extraLarge, colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.primaryContainer)) {
        Row(Modifier.padding(20.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(painterResource(R.drawable.ic_launcher_placeholder), null,
                Modifier.size(64.dp).clip(RoundedCornerShape(20.dp)).background(colorResource(R.color.ic_bg)))
            Spacer(Modifier.width(16.dp))
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.app_name), style = MaterialTheme.typography.headlineSmall, color = MaterialTheme.colorScheme.onPrimaryContainer)
                Text(stringResource(R.string.as_version, BuildConfig.VERSION_NAME), style = MaterialTheme.typography.labelLarge,
                    color = MaterialTheme.colorScheme.onPrimaryContainer)
                Spacer(Modifier.height(4.dp))
                Text(stringResource(R.string.as_about), style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onPrimaryContainer)
            }
        }
    }
}

@Composable
private fun AboutLinkRow(
    icon: ImageVector,
    title: String,
    subtitle: String,
    path: String,
    @DrawableRes avatar: Int? = null,
    author: String = path,
) {
    val ctx = LocalContext.current
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        ListItem(
            headlineContent = { Text(title) },
            supportingContent = { Text(subtitle) },
            leadingContent = { Icon(icon, null, tint = MaterialTheme.colorScheme.primary) },
            colors = ListItemDefaults.colors(containerColor = Color.Transparent),
            modifier = Modifier.weight(1f).clickable(role = Role.Button) { openGithub(ctx, path) },
        )
        if (avatar != null) {
            IconButton(onClick = { openGithub(ctx, author) }, modifier = Modifier.padding(end = 8.dp)) {
                Image(
                    painterResource(avatar),
                    contentDescription = stringResource(R.string.as_open_profile, author),
                    modifier = Modifier.size(36.dp).clip(CircleShape),
                )
            }
        }
    }
}

private fun openGithub(context: Context, path: String) {
    try {
        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("https://github.com/$path")))
    } catch (_: ActivityNotFoundException) {
        Toast.makeText(context, R.string.as_link_unavailable, Toast.LENGTH_SHORT).show()
    }
}

@Composable
private fun Section(title: String, subtitle: String? = null, content: @Composable () -> Unit) {
    Column {
        Text(title, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(start = 8.dp, bottom = if (subtitle == null) 8.dp else 0.dp))
        subtitle?.let {
            Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(start = 8.dp, bottom = 8.dp))
        }
        Card(shape = MaterialTheme.shapes.large, colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer)) {
            Column(Modifier.padding(vertical = 8.dp)) { content() }
        }
    }
}

@Composable
private fun Row_(icon: ImageVector, title: String, sub: String, mono: Boolean = false, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { Text(sub, fontFamily = if (mono) FontFamily.Monospace else null, maxLines = 2) },
        leadingContent = { Icon(icon, null, tint = MaterialTheme.colorScheme.primary) },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent),
        modifier = Modifier.fillMaxWidth().clickable(onClick = onClick),
    )
}
