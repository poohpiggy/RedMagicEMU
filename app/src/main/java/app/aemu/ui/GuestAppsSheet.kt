package app.aemu.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Android
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialShapes
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.material3.toShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import app.aemu.R
import app.aemu.core.GuestApp

/** The firmware's apps: icon, name, version and package; a tap starts the app in the guest. */
@Composable
fun GuestAppsSheet(apps: List<GuestApp>?, onLaunch: (GuestApp) -> Unit, onDismiss: () -> Unit) {
    val state = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var filter by remember { mutableStateOf("") }
    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = state) {
        Column(Modifier.padding(horizontal = 16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.m_apps), style = MaterialTheme.typography.headlineSmallEmphasized, modifier = Modifier.weight(1f))
                if (apps != null) Surface(shape = CircleShape, color = MaterialTheme.colorScheme.secondaryContainer) {
                    Text("${apps.size}", style = MaterialTheme.typography.labelLarge, modifier = Modifier.padding(horizontal = 12.dp, vertical = 4.dp))
                }
            }
            Spacer(Modifier.size(12.dp))
            TextField(filter, { filter = it }, singleLine = true, modifier = Modifier.fillMaxWidth(),
                placeholder = { Text(stringResource(R.string.m_apps_search)) },
                leadingIcon = { Icon(Icons.Rounded.Search, null) },
                trailingIcon = { if (filter.isNotEmpty()) IconButton(onClick = { filter = "" }) { Icon(Icons.Rounded.Close, null) } },
                shape = CircleShape,
                colors = TextFieldDefaults.colors(focusedIndicatorColor = Color.Transparent, unfocusedIndicatorColor = Color.Transparent))
            Spacer(Modifier.size(12.dp))
            if (apps == null) {
                Box(Modifier.fillMaxWidth().padding(48.dp), contentAlignment = Alignment.Center) { LoadingIndicator(Modifier.size(56.dp)) }
                return@Column
            }
            val q = filter.trim()
            val shown = apps.filter { q.isEmpty() || it.label.contains(q, true) || it.pkg.contains(q, true) }
            if (shown.isEmpty()) Text(stringResource(R.string.m_apps_none), Modifier.padding(24.dp))
            LazyColumn(Modifier.heightIn(max = 560.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                items(shown, key = { it.pkg }) { a ->
                    val first = a === shown.first(); val last = a === shown.last()
                    val big = 20.dp; val small = 4.dp
                    Surface(onClick = { onLaunch(a) }, color = MaterialTheme.colorScheme.surfaceContainerHigh,
                        shape = RoundedCornerShape(if (first) big else small, if (first) big else small, if (last) big else small, if (last) big else small),
                        modifier = Modifier.fillMaxWidth()) {
                        Row(Modifier.padding(horizontal = 14.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
                            Box(Modifier.size(48.dp).clip(MaterialShapes.Cookie9Sided.toShape()).background(if (a.icon != null) MaterialTheme.colorScheme.surfaceContainerHighest else MaterialTheme.colorScheme.primaryContainer),
                                contentAlignment = Alignment.Center) {
                                val ic = a.icon
                                if (ic != null) Image(ic.asImageBitmap(), null, Modifier.size(38.dp))
                                else Icon(Icons.Rounded.Android, null, tint = MaterialTheme.colorScheme.onPrimaryContainer)
                            }
                            Spacer(Modifier.width(14.dp))
                            Column(Modifier.weight(1f)) {
                                Text(a.label, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                                Text(a.pkg, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            }
                            if (a.version.isNotEmpty()) Surface(shape = CircleShape, color = MaterialTheme.colorScheme.tertiaryContainer, modifier = Modifier.padding(start = 8.dp)) {
                                Text(a.version.take(14), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onTertiaryContainer,
                                    maxLines = 1, modifier = Modifier.padding(horizontal = 10.dp, vertical = 4.dp))
                            }
                        }
                    }
                }
            }
            Spacer(Modifier.size(16.dp))
        }
    }
}
