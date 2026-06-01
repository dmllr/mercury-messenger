package hela.eillance.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Checkbox
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import hela.eillance.camera.R
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.GroupMember
import hela.eillance.viewmodels.ContactsViewModel

@Composable
fun ContactProfileDialog(
  contact: Contact,
  contactsVM: ContactsViewModel,
  myId: String,
  onDismissRequest: () -> Unit,
  onOverrideSave: (String) -> Unit,
  onOverrideReset: () -> Unit,
  onRemoveRequest: () -> Unit,
  onBlockRequest: () -> Unit
) {
  var overrideName by remember { mutableStateOf("") }

  Dialog(onDismissRequest = onDismissRequest) {
    Card(
      modifier = Modifier
        .fillMaxWidth()
        .padding(16.dp),
      shape = MaterialTheme.shapes.large,
    ) {
      Column {
        // Top "bar" with title and back button
        Row(
          modifier = Modifier.fillMaxWidth(),
          verticalAlignment = Alignment.CenterVertically
        ) {
          IconButton(onClick = onDismissRequest) {
            Icon(
              Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = stringResource(id = R.string.action_close)
            )
          }
          Text(
            text = stringResource(id = R.string.contact_profile_title),
            style = MaterialTheme.typography.titleLarge,
            modifier = Modifier.weight(1f),
            textAlign = TextAlign.Center
          )
          Spacer(Modifier.width(48.dp)) // Balance the IconButton for centering
        }
        HorizontalDivider()

        if (contact.isChannel) {
          ChannelProfileContent(
            contact = contact,
            contactsVM = contactsVM,
            myId = myId,
            onDismissRequest = onDismissRequest
          )
        } else if (contact.isGroup) {
          GroupProfileContent(
            contact = contact,
            contactsVM = contactsVM,
            myId = myId,
            onDismissRequest = onDismissRequest
          )
        } else {
          UserContactProfileContent(
            contact = contact,
            overrideName = overrideName,
            onOverrideNameChange = { overrideName = it },
            onOverrideSave = onOverrideSave,
            onOverrideReset = onOverrideReset,
            onRemoveRequest = onRemoveRequest,
            onBlockRequest = onBlockRequest,
            onDismissRequest = onDismissRequest
          )
        }
      }
    }
  }
}

@Composable
private fun GroupProfileContent(
  contact: Contact,
  contactsVM: ContactsViewModel,
  myId: String,
  onDismissRequest: () -> Unit
) {
  var members by remember { mutableStateOf<List<GroupMember>>(emptyList()) }
  var showAddMemberDialog by remember { mutableStateOf(false) }

  LaunchedEffect(contact.peerId) {
    members = contactsVM.getGroupMembers(contact.peerId)
  }

  val isOwner = contact.ownerId == myId

  if (showAddMemberDialog) {
    val allConfirmed = contactsVM.contacts.value.filter { it.status == ContactStatus.confirmed && !it.isGroup && !it.isChannel }
    AddGroupMemberDialog(
      allContacts = allConfirmed,
      existingMembers = members,
      onDismiss = { showAddMemberDialog = false },
      onAdd = { cid ->
        contactsVM.addGroupMember(contact.peerId, cid)
        members = contactsVM.getGroupMembers(contact.peerId)
        showAddMemberDialog = false
      }
    )
  }

  Column(
    modifier = Modifier.padding(24.dp),
    horizontalAlignment = Alignment.CenterHorizontally
  ) {
    UserAvatar(
      photoBase64 = null,
      modifier = Modifier.size(100.dp),
      fallbackBackground = MaterialTheme.colorScheme.secondaryContainer
    )

    Spacer(modifier = Modifier.height(16.dp))

    Text(text = contact.displayName, style = MaterialTheme.typography.headlineSmall)
    Text(text = contact.peerId, style = MaterialTheme.typography.bodyMedium)

    Spacer(modifier = Modifier.height(24.dp))
    HorizontalDivider()
    Spacer(modifier = Modifier.height(16.dp))

    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
      Text(stringResource(id = R.string.group_participants), style = MaterialTheme.typography.titleMedium)
      if (isOwner) {
        TextButton(onClick = { showAddMemberDialog = true }) {
          Text(stringResource(id = R.string.group_add_members))
        }
      }
    }

    LazyColumn(modifier = Modifier
      .fillMaxWidth()
      .weight(1f, fill = false)) {
      items(members) { member ->
        val memberContact = contactsVM.contacts.value.find { it.peerId == member.peerId }
        val displayName = if (member.peerId == myId) stringResource(R.string.me) else memberContact?.displayName ?: member.peerId.take(8)

        Row(
          modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp),
          horizontalArrangement = Arrangement.SpaceBetween,
          verticalAlignment = Alignment.CenterVertically
        ) {
          Column {
            Text(displayName, style = MaterialTheme.typography.bodyLarge)
            if (member.role == 1) {
              Text(stringResource(id = R.string.group_owner), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
            }
          }

          if (isOwner && member.peerId != myId) {
            TextButton(onClick = {
              contactsVM.removeGroupMember(contact.peerId, member.peerId)
              members = contactsVM.getGroupMembers(contact.peerId)
            }) {
              Text(stringResource(id = R.string.group_remove_member), color = MaterialTheme.colorScheme.error)
            }
          }
        }
      }
    }

    Spacer(modifier = Modifier.height(16.dp))
    HorizontalDivider()
    Spacer(modifier = Modifier.height(16.dp))

    OutlinedButton(
      onClick = {
        contactsVM.leaveGroup(contact.peerId)
        onDismissRequest()
      },
      modifier = Modifier.fillMaxWidth(),
      colors = androidx.compose.material3.ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error)
    ) { Text(stringResource(id = R.string.group_leave)) }
  }
}

@Composable
private fun ChannelProfileContent(
  contact: Contact,
  contactsVM: ContactsViewModel,
  myId: String,
  onDismissRequest: () -> Unit
) {
  var members by remember { mutableStateOf<List<GroupMember>>(emptyList()) }
  var showAddMemberDialog by remember { mutableStateOf(false) }

  LaunchedEffect(contact.peerId) {
    members = contactsVM.getChannelMembers(contact.peerId)
  }

  val isOwner = contact.ownerId == myId || !contact.isReadOnly

  if (showAddMemberDialog) {
    val allConfirmed = contactsVM.contacts.value.filter { it.status == ContactStatus.confirmed && !it.isGroup && !it.isChannel }
    AddGroupMemberDialog(
      allContacts = allConfirmed,
      existingMembers = members,
      onDismiss = { showAddMemberDialog = false },
      onAdd = { cid ->
        contactsVM.addChannelMember(contact.peerId, cid)
        members = contactsVM.getChannelMembers(contact.peerId)
        showAddMemberDialog = false
      }
    )
  }

  Column(
    modifier = Modifier.padding(24.dp),
    horizontalAlignment = Alignment.CenterHorizontally
  ) {
    UserAvatar(
      photoBase64 = null,
      modifier = Modifier.size(100.dp),
      fallbackBackground = MaterialTheme.colorScheme.secondaryContainer
    )

    Spacer(modifier = Modifier.height(16.dp))

    Text(text = contact.displayName, style = MaterialTheme.typography.headlineSmall)
    if (!contact.bio.isNullOrBlank()) {
      Text(text = contact.bio, style = MaterialTheme.typography.bodyMedium)
    }

    Spacer(modifier = Modifier.height(24.dp))
    HorizontalDivider()
    Spacer(modifier = Modifier.height(16.dp))

    Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
      Text(stringResource(id = R.string.channel_subscribers), style = MaterialTheme.typography.titleMedium)
      if (isOwner) {
        TextButton(onClick = { showAddMemberDialog = true }) {
          Text(stringResource(id = R.string.channel_add_subscribers))
        }
      }
    }

    LazyColumn(modifier = Modifier.fillMaxWidth().weight(1f, fill = false)) {
      items(members) { member ->
        val memberContact = contactsVM.contacts.value.find { it.peerId == member.peerId }
        val displayName = if (member.peerId == myId) stringResource(R.string.me) else memberContact?.displayName ?: member.peerId.take(8)

        Row(
          modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
          horizontalArrangement = Arrangement.SpaceBetween,
          verticalAlignment = Alignment.CenterVertically
        ) {
          Column {
            Text(displayName, style = MaterialTheme.typography.bodyLarge)
            if (member.role == 1) {
              Text(stringResource(id = R.string.group_owner), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
            }
          }
          if (isOwner && member.peerId != myId) {
            TextButton(onClick = {
              contactsVM.removeChannelMember(contact.peerId, member.peerId)
              members = contactsVM.getChannelMembers(contact.peerId)
            }) { Text(stringResource(id = R.string.group_remove_member), color = MaterialTheme.colorScheme.error) }
          }
        }
      }
    }

    Spacer(modifier = Modifier.height(16.dp))
    OutlinedButton(onClick = { contactsVM.unsubscribeChannel(contact.peerId); onDismissRequest() }, modifier = Modifier.fillMaxWidth(), colors = androidx.compose.material3.ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error)) {
      Text(stringResource(id = R.string.channel_unsubscribe))
    }
  }
}

@Composable
private fun UserContactProfileContent(
  contact: Contact,
  overrideName: String,
  onOverrideNameChange: (String) -> Unit,
  onOverrideSave: (String) -> Unit,
  onOverrideReset: () -> Unit,
  onRemoveRequest: () -> Unit,
  onBlockRequest: () -> Unit,
  onDismissRequest: () -> Unit
) {
  // Content
  Column(
    modifier = Modifier.padding(24.dp),
    horizontalAlignment = Alignment.CenterHorizontally
  ) {
    UserAvatar(
      photoBase64 = contact.photo,
      modifier = Modifier.size(120.dp),
      fallbackBackground = MaterialTheme.colorScheme.secondaryContainer
    )

    Spacer(modifier = Modifier.height(16.dp))

    Text(text = contact.displayName, style = MaterialTheme.typography.headlineSmall)
    Text(text = contact.peerId, style = MaterialTheme.typography.bodyMedium)

    contact.bio?.let {
      if (it.isNotEmpty()) {
        Spacer(modifier = Modifier.height(8.dp))
        Text(text = it, style = MaterialTheme.typography.bodyLarge)
      }
    }

    Spacer(modifier = Modifier.height(24.dp))
    HorizontalDivider()
    Spacer(modifier = Modifier.height(16.dp))

    Text(
      stringResource(id = R.string.contact_profile_set_alias_title),
      style = MaterialTheme.typography.titleMedium
    )
    Spacer(modifier = Modifier.height(8.dp))

    OutlinedTextField(
      value = overrideName,
      onValueChange = onOverrideNameChange,
      label = { Text(stringResource(id = R.string.contact_profile_alias_label)) },
      singleLine = true,
      keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Words)
    )

    Row(
      modifier = Modifier.fillMaxWidth(),
      horizontalArrangement = Arrangement.SpaceBetween
    ) {
      TextButton(onClick = {
        onOverrideReset()
        onDismissRequest()
      }) {
        Text(stringResource(id = R.string.action_reset))
      }
      Button(
        onClick = {
          onOverrideSave(overrideName)
          onDismissRequest()
        },
        enabled = overrideName.isNotBlank()
      ) {
        Text(stringResource(id = R.string.action_save_alias))
      }
    }

    Spacer(modifier = Modifier.height(16.dp))
    HorizontalDivider()
    Spacer(modifier = Modifier.height(16.dp))

    Button(
      onClick = {
        onBlockRequest()
        onDismissRequest()
      },
      colors = androidx.compose.material3.ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
      modifier = Modifier.fillMaxWidth()
    ) { Text(stringResource(id = R.string.action_block_contact)) }
    OutlinedButton(
      onClick = {
        onRemoveRequest()
        onDismissRequest()
      },
      modifier = Modifier
        .fillMaxWidth()
        .padding(top = 8.dp)
    ) { Text(stringResource(id = R.string.action_delete_contact)) }
  }
}

@Composable
private fun AddGroupMemberDialog(
  allContacts: List<Contact>,
  existingMembers: List<GroupMember>,
  onDismiss: () -> Unit,
  onAdd: (String) -> Unit
) {
  val available = allContacts.filter { c -> existingMembers.none { it.peerId == c.peerId } }
  val selectedContactIds = remember { mutableStateListOf<String>() }

  androidx.compose.material3.AlertDialog(
    onDismissRequest = onDismiss,
    title = { Text(stringResource(id = R.string.group_add_members)) },
    text = {
      if (available.isEmpty()) {
        Text(stringResource(id = R.string.search_no_results))
      } else {
        LazyColumn(modifier = Modifier.height(300.dp)) {
          items(available) { contact ->
            val isSelected = selectedContactIds.contains(contact.peerId)
            Row(
              modifier = Modifier
                .fillMaxWidth()
                .clickable {
                  if (isSelected) selectedContactIds.remove(contact.peerId)
                  else selectedContactIds.add(contact.peerId)
                }
                .padding(vertical = 8.dp),
              verticalAlignment = Alignment.CenterVertically
            ) {
              Checkbox(
                checked = isSelected,
                onCheckedChange = null
              )
              Spacer(modifier = Modifier.width(8.dp))
              Text(contact.displayName)
            }
          }
        }
      }
    },
    confirmButton = {
      TextButton(
        onClick = {
          selectedContactIds.forEach { onAdd(it) }
          onDismiss()
        },
        enabled = selectedContactIds.isNotEmpty()
      ) {
        Text(stringResource(id = R.string.action_add))
      }
    },
    dismissButton = {
      TextButton(onClick = onDismiss) {
        Text(stringResource(id = R.string.action_cancel))
      }
    }
  )
}
