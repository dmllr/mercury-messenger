package hela.eillance.screens.profile

import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardCapitalization.Companion.Sentences
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import hela.eillance.camera.R
import hela.eillance.components.AvatarPicker
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.viewmodels.ProfileViewModel

@Composable
fun ProfileEditDialog(
  myProfile: MyProfileInfo,
  profileVM: ProfileViewModel,
  onDismiss: () -> Unit
) {
  var displayName by remember(myProfile.myDisplayName) {
    mutableStateOf(
      myProfile.myDisplayName ?: ""
    )
  }
  var bio by remember(myProfile.myBio) { mutableStateOf(myProfile.myBio ?: "") }
  var photoBase64 by remember(myProfile.myPhoto) { mutableStateOf(myProfile.myPhoto) }

  Dialog(onDismissRequest = onDismiss) {
      Card(
        shape = MaterialTheme.shapes.large,
        modifier = Modifier.fillMaxWidth()
      ) {
        Column(
          modifier = Modifier
            .padding(16.dp)
            .verticalScroll(rememberScrollState()),
          horizontalAlignment = Alignment.CenterHorizontally
        ) {
          Text(
            stringResource(id = R.string.profile_edit_dialog_title),
            style = MaterialTheme.typography.headlineSmall,
            modifier = Modifier.padding(bottom = 16.dp)
          )

          AvatarPicker(
            hasPhoto = !photoBase64.isNullOrEmpty(),
            onPhotoPicked = { photoBase64 = it },
            onPhotoCleared = { photoBase64 = "" },
            modifier = Modifier
              .size(150.dp)
              .border(2.dp, MaterialTheme.colorScheme.primary, CircleShape),
            fallbackContent = {
              UserAvatar(
                photoBase64 = photoBase64,
                modifier = Modifier.fillMaxSize(),
                fallbackBackground = MaterialTheme.colorScheme.secondaryContainer,
                fallbackTint = MaterialTheme.colorScheme.onSecondaryContainer
              )
            }
          )

          Text(
            stringResource(id = R.string.profile_edit_change_photo_prompt),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = 4.dp)
          )

          Spacer(modifier = Modifier.height(24.dp))

          OutlinedTextField(
            value = displayName,
            onValueChange = { if (it.length <= 64) displayName = it },
            label = { Text(stringResource(id = R.string.profile_edit_display_name_label)) },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Words)
          )

          Spacer(modifier = Modifier.height(16.dp))

          OutlinedTextField(
            value = bio,
            onValueChange = { if (it.length <= 300) bio = it },
            label = { Text(stringResource(id = R.string.profile_edit_bio_label)) },
            modifier = Modifier
              .fillMaxWidth()
              .height(120.dp),
            maxLines = 5,
            keyboardOptions = KeyboardOptions(capitalization = Sentences)
          )

          Spacer(modifier = Modifier.height(24.dp))

          Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.End
          ) {
            TextButton(onClick = onDismiss) {
              Text(stringResource(id = R.string.action_cancel))
            }
            Spacer(modifier = Modifier.width(8.dp))
            Button(onClick = {
              profileVM.updateMyProfile(displayName, bio, photoBase64)
              onDismiss()
            }) {
              Text(stringResource(id = R.string.action_save))
            }
          }
        }
      }
    }
}
