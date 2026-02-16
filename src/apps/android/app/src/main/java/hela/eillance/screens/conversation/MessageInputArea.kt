package hela.eillance.screens.conversation

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.SnippetFolder
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import hela.eillance.camera.R
import hela.eillance.theme.extendedColors
import hela.eillance.viewmodels.DraftAttachment
import java.io.File

private enum class CameraAction { Photo, Video }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MessageInputArea(
  inputValue: String,
  onInputValueChange: (String) -> Unit,
  draftAttachments: List<DraftAttachment>,
  onSendMessage: (String) -> Unit,
  onAddDrafts: (List<Uri>) -> Unit,
  onRemoveDraft: (Int) -> Unit,
  onUpdateNote: (Int, String) -> Unit
) {
  var isFocused by remember { mutableStateOf(false) }
  var selectedDraftIndex by remember { mutableStateOf<Int?>(null) }
  val context = LocalContext.current

  // Media Pickers
  val pickMedia =
    rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia()) { uris ->
      if (uris.isNotEmpty()) onAddDrafts(uris)
    }
  val pickDocs =
    rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
      if (uris.isNotEmpty()) onAddDrafts(uris)
    }

  // Camera Photo
  var cameraPhotoUri by remember { mutableStateOf<Uri?>(null) }
  val takePicture =
    rememberLauncherForActivityResult(ActivityResultContracts.TakePicture()) { success ->
      if (success && cameraPhotoUri != null) {
        onAddDrafts(listOf(cameraPhotoUri!!))
      }
    }

  // Camera Video
  var cameraVideoUri by remember { mutableStateOf<Uri?>(null) }
  val captureVideo =
    rememberLauncherForActivityResult(ActivityResultContracts.CaptureVideo()) { success ->
      if (success && cameraVideoUri != null) {
        onAddDrafts(listOf(cameraVideoUri!!))
      }
    }

  val authority = "${context.packageName}.provider"
  var pendingCameraAction by remember { mutableStateOf<CameraAction?>(null) }

  val launchCameraPhoto = {
    val tmpFile = File.createTempFile("cam_photo_", ".jpg", context.cacheDir)
    val uri = FileProvider.getUriForFile(context, authority, tmpFile)
    cameraPhotoUri = uri
    takePicture.launch(uri)
  }

  val launchCameraVideo = {
    val tmpFile = File.createTempFile("cam_video_", ".mp4", context.cacheDir)
    val uri = FileProvider.getUriForFile(context, authority, tmpFile)
    cameraVideoUri = uri
    captureVideo.launch(uri)
  }

  val permissionLauncher = rememberLauncherForActivityResult(
    ActivityResultContracts.RequestPermission()
  ) { isGranted ->
    if (isGranted) {
      when (pendingCameraAction) {
        CameraAction.Photo -> launchCameraPhoto()
        CameraAction.Video -> launchCameraVideo()
        null -> {}
      }
    }
    pendingCameraAction = null
  }

  fun checkAndLaunch(action: CameraAction) {
    if (ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.CAMERA
      ) == PackageManager.PERMISSION_GRANTED
    ) {
      when (action) {
        CameraAction.Photo -> launchCameraPhoto()
        CameraAction.Video -> launchCameraVideo()
      }
    } else {
      pendingCameraAction = action
      permissionLauncher.launch(Manifest.permission.CAMERA)
    }
  }

  Column {
    // 1. Attachment Action Panel (Visible on Focus)
    AnimatedVisibility(visible = isFocused) {
      Row(
        modifier = Modifier
          .fillMaxWidth()
          .background(MaterialTheme.colorScheme.surfaceContainerHigh)
          .padding(8.dp),
        horizontalArrangement = Arrangement.SpaceEvenly
      ) {
        // Gallery
        IconButton(onClick = {
          pickMedia.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
        }) {
          Icon(
            Icons.Default.Image,
            contentDescription = stringResource(R.string.attach_gallery),
            tint = MaterialTheme.colorScheme.primary
          )
        }

        // Camera Photo
        IconButton(onClick = { checkAndLaunch(CameraAction.Photo) }) {
          Icon(
            Icons.Default.CameraAlt,
            contentDescription = stringResource(R.string.attach_camera_photo),
            tint = MaterialTheme.colorScheme.primary
          )
        }

        // Camera Video
        IconButton(onClick = { checkAndLaunch(CameraAction.Video) }) {
          Icon(
            Icons.Default.Videocam,
            contentDescription = stringResource(R.string.attach_camera_video),
            tint = MaterialTheme.colorScheme.primary
          )
        }

        // Document (Readable docs)
        IconButton(onClick = {
          pickDocs.launch(arrayOf("application/*", "text/*"))
        }) {
          Icon(
            Icons.Default.Description,
            contentDescription = stringResource(R.string.attach_document),
            tint = MaterialTheme.colorScheme.primary
          )
        }

        // Binary File (All files)
        IconButton(onClick = { pickDocs.launch(arrayOf("*/*")) }) {
          Icon(
            Icons.Default.SnippetFolder,
            contentDescription = stringResource(R.string.attach_file),
            tint = MaterialTheme.colorScheme.primary
          )
        }
      }
    }

    // 2. Draft Attachments Preview Row
    AnimatedVisibility(visible = draftAttachments.isNotEmpty()) {
      LazyRow(
        modifier = Modifier
          .fillMaxWidth()
          .background(MaterialTheme.colorScheme.surfaceVariant)
          .padding(8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp)
      ) {
        itemsIndexed(draftAttachments) { index, draft ->
          DraftAttachmentItem(
            draft = draft,
            onRemove = { onRemoveDraft(index) },
            onClick = { selectedDraftIndex = index }
          )
        }
      }
    }

    // 3. Text Input & Send
    Row(
      modifier = Modifier
        .fillMaxWidth()
        .background(MaterialTheme.colorScheme.surface)
        .padding(horizontal = 8.dp, vertical = 4.dp),
      verticalAlignment = Alignment.CenterVertically
    ) {
      TextField(
        value = inputValue,
        onValueChange = onInputValueChange,
        modifier = Modifier
          .weight(1f)
          .onFocusChanged { isFocused = it.isFocused },
        placeholder = { Text(stringResource(R.string.msg_input_placeholder)) },
        colors = TextFieldDefaults.colors(
          focusedIndicatorColor = Color.Transparent,
          unfocusedIndicatorColor = Color.Transparent,
          disabledIndicatorColor = Color.Transparent
        ),
        keyboardOptions = KeyboardOptions(
          capitalization = KeyboardCapitalization.Sentences,
          keyboardType = KeyboardType.Text,
          imeAction = ImeAction.Default
        ),
        maxLines = 4
      )

      val canSend = inputValue.isNotBlank() || draftAttachments.isNotEmpty()
      IconButton(
        modifier = Modifier
          .background(
            if (canSend) MaterialTheme.extendedColors.success else MaterialTheme.colorScheme.surfaceVariant,
            shape = CircleShape
          ),
        onClick = {
          if (canSend) {
            onSendMessage(inputValue)
          }
        },
        enabled = canSend
      ) {
        Icon(
          Icons.AutoMirrored.Filled.Send,
          contentDescription = stringResource(R.string.send_description),
          tint = if (canSend) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurfaceVariant
        )
      }
    }
  }

  if (selectedDraftIndex != null && draftAttachments.isNotEmpty()) {
    val safeIndex = selectedDraftIndex!!.coerceIn(0, draftAttachments.lastIndex)
    DraftAttachmentEditor(
      drafts = draftAttachments,
      initialIndex = safeIndex,
      onDismiss = { selectedDraftIndex = null },
      onRemove = { index ->
        onRemoveDraft(index)
        if (draftAttachments.isEmpty()) {
          selectedDraftIndex = null
        }
      },
      onNoteChange = onUpdateNote
    )
  }
}
