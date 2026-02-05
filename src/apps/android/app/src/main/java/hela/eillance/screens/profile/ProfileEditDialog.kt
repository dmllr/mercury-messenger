package hela.eillance.screens.profile

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Base64
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.viewmodels.ProfileViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream

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

  var rawSelectedImage by remember { mutableStateOf<Bitmap?>(null) }
  var showImageEditor by remember { mutableStateOf(false) }

  val context = LocalContext.current
  val scope = rememberCoroutineScope()
  val snackbarHostState = remember { SnackbarHostState() }

  val imagePickerLauncher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.GetContent()
  ) { uri ->
    uri?.let {
      scope.launch(Dispatchers.IO) {
        try {
          context.contentResolver.openInputStream(it)?.use { inputStream ->
            val bitmap = BitmapFactory.decodeStream(inputStream)
            if (bitmap != null) {
              withContext(Dispatchers.Main) {
                rawSelectedImage = bitmap
                showImageEditor = true
              }
            } else {
              snackbarHostState.showSnackbar(context.getString(R.string.profile_edit_image_decode_fail))
            }
          }
        } catch (e: Exception) {
          e.printStackTrace()
          snackbarHostState.showSnackbar(context.getString(R.string.profile_edit_image_load_fail))
        }
      }
    }
  }

  if (showImageEditor && rawSelectedImage != null) {
    ImageEditorDialog(
      bitmap = rawSelectedImage!!,
      onDismiss = {
        showImageEditor = false
        rawSelectedImage = null
      },
      onSave = { croppedBitmap ->
        scope.launch(Dispatchers.Default) {
          val outputStream = ByteArrayOutputStream()
          croppedBitmap.compress(Bitmap.CompressFormat.JPEG, 85, outputStream)
          val byteArray = outputStream.toByteArray()
          val base64 = Base64.encodeToString(byteArray, Base64.NO_WRAP)
          withContext(Dispatchers.Main) {
            photoBase64 = base64
            showImageEditor = false
            rawSelectedImage = null
          }
        }
      }
    )
  } else {
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

          Box(
            modifier = Modifier
              .size(150.dp)
              .clip(CircleShape)
              .border(2.dp, MaterialTheme.colorScheme.primary, CircleShape)
              .clickable { imagePickerLauncher.launch("image/*") },
            contentAlignment = Alignment.Center
          ) {
            UserAvatar(
              photoBase64 = photoBase64,
              modifier = Modifier.fillMaxSize(),
              fallbackBackground = MaterialTheme.colorScheme.secondaryContainer,
              fallbackTint = MaterialTheme.colorScheme.onSecondaryContainer
            )
          }

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
}
