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
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import hela.eillance.camera.R
import hela.eillance.ndkio.ContactStatus
import hela.eillance.theme.ListItem
import hela.eillance.viewmodels.ContactsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RejectedContactsScreen(contactsVM: ContactsViewModel, onBack: () -> Unit) {
  val contacts by contactsVM.contacts.collectAsState()
  val rejectedContacts = contacts.filter { it.status == ContactStatus.rejected }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_rejected_contacts)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(
              Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = stringResource(id = R.string.action_back)
            )
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
      ) { Text(stringResource(id = R.string.rejected_contacts_empty)) }
    } else {
      LazyColumn(modifier = Modifier.padding(padding)) {
        items(rejectedContacts, key = { it.peerId }) { contact ->
          ListItem(
            headlineContent = { Text(contact.name) },
            supportingContent = { Text(contact.peerId) },
            trailingContent = {
              Button(onClick = { contactsVM.unblockContact(contact.peerId) }) {
                Text(
                  stringResource(
                    id = R.string.action_unblock
                  )
                )
              }
            }
          )
          HorizontalDivider()
        }
      }
    }
  }
}
