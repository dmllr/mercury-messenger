package hela.eillance.screens.conversation

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.SnippetFolder
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.request.videoFrameMillis
import hela.eillance.camera.R
import hela.eillance.theme.extendedColors
import hela.eillance.viewmodels.DraftAttachment
import java.io.File

private enum class CameraAction { Photo, Video }

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
fun MessageInputArea(
  draftAttachments: List<DraftAttachment>,
  onSendMessage: (String) -> Unit,
  onAddDrafts: (List<Uri>) -> Unit,
  onRemoveDraft: (Int) -> Unit,
  onUpdateNote: (Int, String) -> Unit
) {
  var text by remember { mutableStateOf(TextFieldValue("")) }
  var isFocused by remember { mutableStateOf(false) }
  var selectedDraftIndex by remember { mutableStateOf<Int?>(null) }
  val context = LocalContext.current

  // Media Pickers
  val pickMedia = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia()) { uris ->
    if (uris.isNotEmpty()) onAddDrafts(uris)
  }
  val pickDocs = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
    if (uris.isNotEmpty()) onAddDrafts(uris)
  }

  // Camera Photo
  var cameraPhotoUri by remember { mutableStateOf<Uri?>(null) }
  val takePicture = rememberLauncherForActivityResult(ActivityResultContracts.TakePicture()) { success ->
    if (success && cameraPhotoUri != null) {
      onAddDrafts(listOf(cameraPhotoUri!!))
    }
  }

  // Camera Video
  var cameraVideoUri by remember { mutableStateOf<Uri?>(null) }
  val captureVideo = rememberLauncherForActivityResult(ActivityResultContracts.CaptureVideo()) { success ->
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
    if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
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
        value = text,
        onValueChange = { text = it },
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

      val canSend = text.text.isNotBlank() || draftAttachments.isNotEmpty()
      IconButton(
        modifier = Modifier
          .background(
            if (canSend) MaterialTheme.extendedColors.success else MaterialTheme.colorScheme.surfaceVariant,
            shape = CircleShape
          ),
        onClick = {
          if (canSend) {
            onSendMessage(text.text)
            text = TextFieldValue("")
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

@Composable
fun DraftAttachmentItem(
  draft: DraftAttachment,
  onRemove: () -> Unit,
  onClick: () -> Unit
) {
  Box(
    modifier = Modifier
      .width(100.dp)
      .clip(RoundedCornerShape(8.dp))
      .background(MaterialTheme.colorScheme.surface)
      .clickable(onClick = onClick)
  ) {
    Column {
      Box(
        modifier = Modifier
          .height(80.dp)
          .fillMaxWidth()
      ) {
        if (draft.bitmap != null) {
          Image(
            bitmap = draft.bitmap.asImageBitmap(),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize()
          )
        } else {
          val icon = getDraftIcon(draft.mimeType)
          Box(
            modifier = Modifier
              .fillMaxSize()
              .background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center
          ) {
            Icon(
              imageVector = icon,
              contentDescription = null,
              tint = MaterialTheme.colorScheme.onSurfaceVariant,
              modifier = Modifier.size(32.dp)
            )
          }
        }

        IconButton(
          onClick = onRemove,
          modifier = Modifier
            .align(Alignment.TopEnd)
            .size(24.dp)
            .background(Color.Black.copy(alpha = 0.5f), CircleShape)
        ) {
          Icon(
            Icons.Default.Close,
            contentDescription = stringResource(R.string.attachment_remove),
            tint = Color.White,
            modifier = Modifier.size(16.dp)
          )
        }
      }

      Column(modifier = Modifier.padding(4.dp)) {
        Text(
          text = draft.name,
          style = MaterialTheme.typography.labelSmall,
          fontWeight = FontWeight.Bold,
          maxLines = 1,
          overflow = TextOverflow.Ellipsis
        )
        if (draft.note.isNotEmpty()) {
          Text(
            text = draft.note,
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            color = MaterialTheme.colorScheme.primary
          )
        } else {
          Text(
            text = stringResource(R.string.attachment_note_placeholder),
            style = MaterialTheme.typography.labelSmall,
            fontStyle = FontStyle.Italic,
            color = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
      }
    }
  }
}

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
fun DraftAttachmentEditor(
  drafts: List<DraftAttachment>,
  initialIndex: Int,
  onDismiss: () -> Unit,
  onRemove: (Int) -> Unit,
  onNoteChange: (Int, String) -> Unit
) {
  val pagerState = rememberPagerState(initialPage = initialIndex, pageCount = { drafts.size })
  val context = LocalContext.current

  Dialog(
    onDismissRequest = onDismiss,
    properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)
  ) {
    Scaffold(
      containerColor = Color.Black,
      contentWindowInsets = WindowInsets.systemBars.union(WindowInsets.ime),
      topBar = {
        TopAppBar(
          title = {
            Text(
              stringResource(
                id = R.string.attachment_editor_pager_indicator,
                pagerState.currentPage + 1,
                drafts.size
              ), color = Color.White
            )
          },
          navigationIcon = {
            IconButton(onClick = onDismiss) {
              Icon(
                Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = stringResource(id = R.string.action_back),
                tint = Color.White
              )
            }
          },
          actions = {
            IconButton(onClick = { onRemove(pagerState.currentPage) }) {
              Icon(
                Icons.Default.Delete,
                contentDescription = stringResource(id = R.string.action_delete),
                tint = Color.White
              )
            }
          },
          colors = TopAppBarDefaults.topAppBarColors(containerColor = Color.Black.copy(alpha = 0.5f))
        )
      }
    ) { padding ->
      Box(
        modifier = Modifier
          .fillMaxSize()
          .padding(padding)
      ) {
        HorizontalPager(
          state = pagerState,
          modifier = Modifier.fillMaxSize()
        ) { page ->
          val draft = drafts[page]
          Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            val isVideo = draft.mimeType.startsWith("video/")
            val isImage = draft.mimeType.startsWith("image/")

            if (isImage || isVideo) {
              val model = if (isVideo) {
                ImageRequest.Builder(context)
                  .data(draft.uri)
                  .decoderFactory { result, options, _ -> VideoFrameDecoder(result.source, options) }
                  .videoFrameMillis(1000)
                  .build()
              } else draft.uri

              AsyncImage(
                model = model,
                contentDescription = null,
                contentScale = ContentScale.Fit,
                modifier = Modifier.fillMaxSize()
              )
            } else {
              Icon(
                imageVector = getDraftIcon(draft.mimeType),
                contentDescription = null,
                tint = Color.Gray,
                modifier = Modifier.size(120.dp)
              )
            }
          }
        }

        Box(
          modifier = Modifier
            .align(Alignment.BottomCenter)
            .fillMaxWidth()
            .background(
              Brush.verticalGradient(
                colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.9f))
              )
            )
            .padding(16.dp)
            .padding(bottom = 16.dp)
        ) {
          TextField(
            value = drafts[pagerState.currentPage].note,
            onValueChange = { onNoteChange(pagerState.currentPage, it) },
            placeholder = {
              Text(
                stringResource(R.string.attachment_note_placeholder),
                color = Color.LightGray
              )
            },
            colors = TextFieldDefaults.colors(
              focusedContainerColor = Color.Transparent,
              unfocusedContainerColor = Color.Transparent,
              focusedTextColor = Color.White,
              unfocusedTextColor = Color.White,
              cursorColor = MaterialTheme.colorScheme.primary,
              focusedIndicatorColor = Color.White.copy(alpha = 0.5f),
              unfocusedIndicatorColor = Color.White.copy(alpha = 0.3f)
            ),
            modifier = Modifier.fillMaxWidth(),
            maxLines = 4
          )
        }
      }
    }
  }
}

private fun getDraftIcon(mimeType: String): ImageVector {
  return when {
    mimeType == "application/pdf" -> Icons.Default.Description
    mimeType.startsWith("audio/") -> Icons.Default.AudioFile
    mimeType.startsWith("video/") -> Icons.Default.Videocam
    mimeType.startsWith("image/") -> Icons.Default.Image
    else -> Icons.Default.SnippetFolder
  }
}
