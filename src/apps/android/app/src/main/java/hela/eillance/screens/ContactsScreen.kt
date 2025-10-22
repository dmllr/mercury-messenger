package hela.eillance.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Person
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.MainViewModel
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.PeerConnectionState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
    viewModel: MainViewModel,
    uiState: AppState,
    onContactClick: (String) -> Unit,
    onNavigateToProfile: () -> Unit,
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Mercury") },
                actions = {
                    IconButton(onClick = onNavigateToProfile) {
                        Icon(Icons.Default.Person, contentDescription = "Profile")
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
                        onResend = { viewModel.addContact(contact.peerId) }
                    )
                    HorizontalDivider(color = DividerDefaults.color.copy(alpha = 0.5f))
                }
            }
        }
    }
}

@Composable
fun ContactListItem(contact: Contact, onClick: () -> Unit, onAccept: () -> Unit, onResend: () -> Unit) {
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
        headlineContent = {
            Text(
                text = contact.peerId,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.SemiBold
            )
        },
        supportingContent = {
            Column {
                Text(text = statusText, color = statusColor, style = MaterialTheme.typography.bodyMedium)
                if (connectionText.isNotEmpty()) {
                    Text(text = connectionText, color = connectionColor, style = MaterialTheme.typography.bodySmall)
                }
            }
        },
        trailingContent = {
            when (contact.status) {
                ContactStatus.awaitingApproval -> {
                    Button(onClick = onAccept) {
                        Text("Accept")
                    }
                }
                ContactStatus.pendingApproval -> {
                    TextButton(onClick = onResend) {
                        Text("Resend")
                    }
                }
                else -> {}
            }
        },
        modifier = Modifier.clickable(
            // Only allow clicking confirmed contacts to open the interaction screen
            enabled = contact.status == ContactStatus.confirmed,
            onClick = onClick
        ),
        colors = ListItemDefaults.colors(containerColor = Color.Transparent)
    )
}
