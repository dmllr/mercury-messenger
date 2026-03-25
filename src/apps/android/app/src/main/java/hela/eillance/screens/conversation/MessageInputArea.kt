package hela.eillance.screens.conversation

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
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
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.theme.extendedColors
import hela.eillance.ndkio.DraftAttachment
import java.io.File
import androidx.core.net.toUri

private enum class CameraAction { Photo, Video }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MessageInputArea(
  inputValue: String,
  onInputValueChange: (String) -> Unit,
  draftAttachments: List<DraftAttachment>,
  isFocused: Boolean,
  onFocusChange: (Boolean) -> Unit,
  replyingToMessage: HistoryEntry? = null,
  replyingToName: String? = null,
  onCancelReply: () -> Unit = {},
  onSendMessage: (String) -> Unit,
  onAddDrafts: (List<Uri>) -> Unit,
  onRemoveDraft: (Int) -> Unit,
  onUpdateNote: (Int, String) -> Unit
) {
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
  var cameraPhotoUriString by rememberSaveable { mutableStateOf<String?>(null) }
  val takePicture =
    rememberLauncherForActivityResult(ActivityResultContracts.TakePicture()) { success ->
      if (success && cameraPhotoUriString != null) {
        onAddDrafts(listOf(cameraPhotoUriString!!.toUri()))
      }
    }

  // Camera Video
  var cameraVideoUriString by rememberSaveable { mutableStateOf<String?>(null) }
  val captureVideo =
    rememberLauncherForActivityResult(ActivityResultContracts.CaptureVideo()) { success ->
      if (success && cameraVideoUriString != null) {
        onAddDrafts(listOf(cameraVideoUriString!!.toUri()))
      }
    }

  val authority = "${context.packageName}.provider"
  var pendingCameraAction by remember { mutableStateOf<CameraAction?>(null) }

  val launchCameraPhoto = {
    val tmpFile = File.createTempFile("cam_photo_", ".jpg", context.cacheDir)
    val uri = FileProvider.getUriForFile(context, authority, tmpFile)
    cameraPhotoUriString = uri.toString()
    takePicture.launch(uri)
  }

  val launchCameraVideo = {
    val tmpFile = File.createTempFile("cam_video_", ".mp4", context.cacheDir)
    val uri = FileProvider.getUriForFile(context, authority, tmpFile)
    cameraVideoUriString = uri.toString()
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
    AnimatedVisibility(visible = replyingToMessage != null) {
      Box(modifier = Modifier.padding(start = 8.dp, end = 8.dp, top = 8.dp)) {
        ReplyPreview(
          name = replyingToName.orEmpty(),
          text = replyingToMessage?.content.orEmpty(),
          hasAttachments = replyingToMessage?.attachments?.isNotEmpty() == true,
          modifier = Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant),
          onClose = onCancelReply
        )
      }
    }
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

    val canSend = inputValue.isNotBlank() || draftAttachments.isNotEmpty()
    val showControls = isFocused || canSend

    // 3. Text Input & Send
    TextField(
      value = inputValue,
      onValueChange = onInputValueChange,
      minLines = 1,
      modifier = Modifier
        .fillMaxWidth()
        .background(MaterialTheme.colorScheme.surface)
        .padding(horizontal = 8.dp, vertical = 4.dp)
        .animateContentSize()
        .onFocusChanged {
          onFocusChange(it.isFocused)
        },
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
      maxLines = 4,
      trailingIcon = {
        if (showControls) {
          Column(
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.padding(end = 4.dp)
          ) {
            IconButton(
              onClick = { if (canSend) onSendMessage(inputValue) },
              enabled = canSend,
              modifier = Modifier.size(36.dp)
            ) {
              Box(
                modifier = Modifier
                  .fillMaxSize()
                  .background(if (canSend) MaterialTheme.extendedColors.success else Color.Transparent, CircleShape),
                contentAlignment = Alignment.Center
              ) {
                Icon(
                  Icons.AutoMirrored.Filled.Send,
                  contentDescription = stringResource(R.string.send_description),
                  tint = if (canSend) MaterialTheme.colorScheme.onPrimary else MaterialTheme.colorScheme.onSurfaceVariant,
                  modifier = Modifier.size(20.dp)
                )
              }
            }
          }
        }
      }
    )
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
