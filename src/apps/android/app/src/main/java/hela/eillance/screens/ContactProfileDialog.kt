package hela.eillance.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
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

@Composable
fun ContactProfileDialog(
  contact: Contact,
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
            onValueChange = { overrideName = it },
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
            colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error),
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
    }
  }
}
