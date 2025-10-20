package hela.eillance.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.AddContactDialog
import hela.eillance.MainViewModel
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
    viewModel: MainViewModel,
    contacts: List<Contact>,
    onContactClick: (String) -> Unit
) {
    val showDialog by viewModel.showAddContactDialog.collectAsState()

    if (showDialog) {
        AddContactDialog(
            onDismissRequest = { viewModel.onAddContactDialogDismiss() },
            onConfirm = { peerId -> viewModel.addContact(peerId) }
        )
    }

    Scaffold(
        topBar = { TopAppBar(title = { Text("Contacts") }) },
        floatingActionButton = {
            FloatingActionButton(onClick = { viewModel.onAddContactClicked() }) {
                Icon(Icons.Default.Add, contentDescription = "Add Contact")
            }
        }
    ) { padding ->
        if (contacts.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize().padding(padding),
                contentAlignment = Alignment.Center
            ) {
                Text("No contacts yet. Add one!", style = MaterialTheme.typography.bodyLarge)
            }
        } else {
            LazyColumn(modifier = Modifier.padding(padding)) {
                items(contacts, key = { it.peer_id }) { contact ->
                    ContactListItem(
                        contact = contact,
                        onClick = { onContactClick(contact.peer_id) },
                        onAccept = { viewModel.acceptContact(contact.peer_id) }
                    )
                    Divider()
                }
            }
        }
    }
}

@Composable
fun ContactListItem(contact: Contact, onClick: () -> Unit, onAccept: () -> Unit) {
    ListItem(
        headlineContent = {
            Text(
                text = contact.peer_id,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        },
        supportingContent = {
            Text(
                text = when (contact.status) {
                    ContactStatus.confirmed -> "Confirmed"
                    ContactStatus.pendingApproval -> "Pending approval"
                    ContactStatus.awaitingApproval -> "Awaiting your approval"
                }
            )
        },
        trailingContent = {
            if (contact.status == ContactStatus.awaitingApproval) {
                Button(onClick = onAccept) {
                    Text("Accept")
                }
            }
        },
        modifier = Modifier.clickable(onClick = onClick)
    )
}
