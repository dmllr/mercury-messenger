package hela.eillance.screens

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import hela.eillance.MainViewModel
import hela.eillance.ndkio.ContactStatus

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RejectedContactsScreen(viewModel: MainViewModel, onBack: () -> Unit) {
  val contacts by viewModel.contacts.collectAsState()
  val rejectedContacts = contacts.filter { it.status == ContactStatus.rejected }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text("Blocked Contacts") },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
          }
        }
      )
    }
  ) { padding ->
    if (rejectedContacts.isEmpty()) {
      Box(
        modifier = Modifier
          .fillMaxSize()
          .padding(padding), contentAlignment = Alignment.Center
      ) {
        Text("No contacts have been blocked.")
      }
    } else {
      LazyColumn(modifier = Modifier.padding(padding)) {
        items(rejectedContacts, key = { it.peerId }) { contact ->
          ListItem(
            headlineContent = { Text(contact.name) },
            supportingContent = { Text(contact.peerId) },
            trailingContent = {
              Button(onClick = { viewModel.unblockContact(contact.peerId) }) {
                Text("Unblock")
              }
            }
          )
          HorizontalDivider()
        }
      }
    }
  }
}
