package hela.eillance.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Person
import androidx.compose.material3.Badge
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import coil.compose.rememberAsyncImagePainter
import hela.eillance.MainViewModel
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.theme.extendedColors

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
    viewModel: MainViewModel,
    uiState: AppState,
    onContactClick: (String) -> Unit,
    onNavigateToProfile: () -> Unit,
    onShowContactProfile: (String) -> Unit
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Mercury") },
                actions = {
                    Box {
                        IconButton(onClick = onNavigateToProfile) {
                            Icon(Icons.Default.Person, contentDescription = "Profile")
                        }
                        val connectionStatus = ServiceConnectionState.fromInt(uiState.connectionState)
                        if (connectionStatus != ServiceConnectionState.Unknown) {
                            val dotColor = when (connectionStatus) {
                                ServiceConnectionState.Online -> MaterialTheme.extendedColors.success
                                ServiceConnectionState.Failure -> MaterialTheme.colorScheme.error
                                else -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f)
                            }
                            // Status dot
                            Box(
                                modifier = Modifier
                                    .padding(bottom = 4.dp, end = 4.dp)
                                    .size(12.dp)
                                    .background(color = dotColor, shape = CircleShape)
                                    .align(Alignment.BottomEnd)
                            )
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer
                )
            )
        },
        floatingActionButton = {
            FloatingActionButton(onClick = { viewModel.onAddContactClicked() }) {
                Icon(Icons.Default.Add, contentDescription = "Add Contact")
            }
        }
    ) { padding ->
        if (uiState.contacts.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(padding),
                contentAlignment = Alignment.Center
            ) {
                if (uiState.myId.isEmpty()) {
                    CircularProgressIndicator()
                } else {
                    Text("No contacts yet. Add one!", style = MaterialTheme.typography.bodyLarge)
                }
            }
        } else {
            LazyColumn(modifier = Modifier.padding(padding)) {
                items(uiState.contacts, key = { it.peerId }) { contact ->
                    ContactListItem(
                        contact = contact,
                        onClick = { onContactClick(contact.peerId) },
                        onAccept = { viewModel.acceptContact(contact.peerId) },
                        onResend = { viewModel.addContact(contact.peerId) },
                        onShowProfile = { onShowContactProfile(contact.peerId) }
                    )
                    HorizontalDivider(color = DividerDefaults.color.copy(alpha = 0.5f))
                }
            }
        }
    }
}

@Composable
fun ContactListItem(
    contact: Contact,
    onClick: () -> Unit,
    onAccept: () -> Unit,
    onResend: () -> Unit,
    onShowProfile: () -> Unit
) {
    val statusText = when (contact.status) {
        ContactStatus.confirmed -> "Confirmed"
        ContactStatus.pendingApproval -> "Request sent"
        ContactStatus.awaitingApproval -> "New request"
    }
    val statusColor = when (contact.status) {
        ContactStatus.confirmed -> MaterialTheme.colorScheme.primary
        ContactStatus.pendingApproval -> MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
        ContactStatus.awaitingApproval -> MaterialTheme.colorScheme.tertiary
    }

    val connectionStatus = PeerConnectionState.fromInt(contact.connectionState)
    val (connectionText, connectionColor) = when (connectionStatus) {
        PeerConnectionState.Connecting -> "Connecting..." to MaterialTheme.colorScheme.secondary
        PeerConnectionState.Connected -> "Connected" to MaterialTheme.colorScheme.primary
        PeerConnectionState.Failed -> "Failed" to MaterialTheme.colorScheme.error
        else -> "" to Color.Transparent
    }

    ListItem(
        leadingContent = {
            val painter = rememberAsyncImagePainter(
                model = "data:image/png;base64," + contact.photo,
                onError = {
                    // You can log the error if needed
                }
            )
            if (contact.photo != null) {
                Image(
                    painter = painter,
                    contentDescription = "Profile Photo",
                    modifier = Modifier
                        .size(40.dp)
                        .clip(CircleShape),
                    contentScale = ContentScale.Crop
                )
            } else {
                Icon(
                    imageVector = Icons.Default.Person,
                    contentDescription = "Default Profile Icon",
                    modifier = Modifier
                        .size(40.dp)
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.secondaryContainer)
                        .padding(8.dp)
                )
            }
        },
        headlineContent = {
            Text(
                text = contact.name,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.SemiBold
            )
        },
        supportingContent = {
            Column {
                Text(
                    text = contact.peerId,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.alpha(0.7f)
                )
                if (connectionText.isNotEmpty() && contact.status == ContactStatus.confirmed) {
                    Text(text = connectionText, color = connectionColor, style = MaterialTheme.typography.bodySmall)
                }
            }
        },
        trailingContent = {
            when (contact.status) {
                ContactStatus.awaitingApproval -> {
                    Row {
                        Button(onClick = onAccept) { Text("Accept") }
                    }
                }
                ContactStatus.pendingApproval -> {
                    TextButton(onClick = onResend) { Text("Resend") }
                }
                else -> {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        if (contact.unreadCount > 0) {
                            Badge { Text(contact.unreadCount.toString()) }
                            Spacer(Modifier.width(8.dp))
                        }
                        IconButton(onClick = onShowProfile) {
                            Icon(Icons.Default.MoreVert, contentDescription = "Contact options")
                        }
                    }
                }
            }
        },
        modifier = Modifier.clickable(enabled = contact.status == ContactStatus.confirmed, onClick = onClick),
        colors = ListItemDefaults.colors(containerColor = Color.Transparent)
    )
}
