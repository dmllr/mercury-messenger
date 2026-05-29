package hela.eillance.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Contact

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CreateGroupDialog(
  contacts: List<Contact>,
  onDismissRequest: () -> Unit,
  onCreateGroup: (String, List<String>) -> Unit
) {
  var groupName by remember { mutableStateOf("") }
  val selectedContactIds = remember { mutableStateListOf<String>() }

  AlertDialog(
    onDismissRequest = onDismissRequest,
    title = { Text(stringResource(id = R.string.group_new)) },
    text = {
      Column {
        OutlinedTextField(
          value = groupName,
          onValueChange = { groupName = it },
          label = { Text(stringResource(id = R.string.group_name_label)) },
          singleLine = true,
          modifier = Modifier.fillMaxWidth()
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(stringResource(id = R.string.group_add_members))
        Spacer(modifier = Modifier.height(8.dp))
        LazyColumn(modifier = Modifier.weight(1f, fill = false).heightIn(max = 250.dp)) {
          items(contacts) { contact ->
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
          if (groupName.isNotBlank() && selectedContactIds.isNotEmpty()) {
            onCreateGroup(groupName, selectedContactIds.toList())
            onDismissRequest()
          }
        },
        enabled = groupName.isNotBlank() && selectedContactIds.isNotEmpty()
      ) {
        Text(stringResource(id = R.string.group_create_action))
      }
    },
    dismissButton = {
      TextButton(onClick = onDismissRequest) {
        Text(stringResource(id = R.string.action_cancel))
      }
    }
  )
}
