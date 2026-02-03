package hela.eillance.screens

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.CallMade
import androidx.compose.material.icons.automirrored.filled.CallMissed
import androidx.compose.material.icons.automirrored.filled.CallReceived
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.DoneAll
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material.icons.filled.SnippetFolder
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
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
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.content.FileProvider
import androidx.core.os.ConfigurationCompat
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.request.videoFrameMillis
import hela.eillance.BannerType
import hela.eillance.MainViewModel
import hela.eillance.camera.R
import hela.eillance.components.MediaBubbleContent
import hela.eillance.components.StatusSnackbar
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.theme.extendedColors
import hela.eillance.ui.LocalizationMapping
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import hela.eillance.viewmodels.DraftAttachment
import kotlinx.coroutines.launch
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.io.File
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import java.util.concurrent.TimeUnit
import kotlin.math.abs

private val jsonParser = Json { ignoreUnknownKeys = true }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConversationScreen(
  chatVM: ConversationViewModel,
  contactsVM: ContactsViewModel,
  callVM: CallViewModel,
  mainViewModel: MainViewModel,
  peerId: String,
  contact: Contact?,
  historyPage: HistoryPage,
  callState: CallState,
  connectionState: ServiceConnectionState,
  onBack: () -> Unit,
  onNavigateToCall: (String) -> Unit,
  onShowContactProfile: (String) -> Unit,
  onMediaClick: (List<Attachment>, Int) -> Unit
) {
  val listState = rememberLazyListState()
  val coroutineScope = rememberCoroutineScope()
  val transferProgress by chatVM.transferProgress.collectAsState()
  val draftAttachments by chatVM.draftAttachments.collectAsState()
  val context = LocalContext.current
  val bannerState by mainViewModel.permissionBannerState.collectAsState()

  // State for permissions launchers
  val notifLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { mainViewModel.checkAllPermissions() }
  val callLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { mainViewModel.checkAllPermissions() }

  var previousHistorySize by remember(peerId) { mutableIntStateOf(historyPage.messages.size) }

  var systemMessageToShowDetails by remember { mutableStateOf<HistoryEntry?>(null) }
  var tappedTruncatedMessageId by remember { mutableStateOf<Long?>(null) }

  // Auto-navigation to call screen
  LaunchedEffect(callState.isConnecting, callState.isRinging, callState.isInCall, callState.peerId) {
    if ((callState.isConnecting || callState.isRinging || callState.isInCall) && callState.peerId == peerId) {
      onNavigateToCall(peerId)
    }
  }

  // Scroll to bottom when new messages arrive
  LaunchedEffect(historyPage.messages.size) {
    if (historyPage.messages.size > previousHistorySize) {
      if (listState.firstVisibleItemIndex < 3) {
        coroutineScope.launch {
          listState.animateScrollToItem(0)
        }
      }
    }
    previousHistorySize = historyPage.messages.size
  }

  // Mark messages as read
  LaunchedEffect(historyPage.messages) {
    val unreadMessageIds = historyPage.messages
      .filter { it.kind == HistoryEntryKind.TextMessage && it.direction == MessageDirection.Received && it.status != MessageStatus.Read }
      .map { it.id }

    if (unreadMessageIds.isNotEmpty()) {
      chatVM.markMessagesAsRead(unreadMessageIds)
    }
  }

  // Logic to handle tapping "Read more" on a truncated message
  val loadedMessage = if (tappedTruncatedMessageId != null) {
    historyPage.messages.find { it.id == tappedTruncatedMessageId && !it.isTruncated }
  } else {
    null
  }

  LaunchedEffect(loadedMessage) {
    if (loadedMessage != null) {
      systemMessageToShowDetails = loadedMessage
      tappedTruncatedMessageId = null
    }
  }

  systemMessageToShowDetails?.let { entry ->
    SystemMessageDetailsDialog(entry = entry, onDismiss = { systemMessageToShowDetails = null })
  }

  Scaffold(
    topBar = {
      TopAppBar(
        title = {
          Row(
            modifier = Modifier.clickable { onShowContactProfile(peerId) },
            verticalAlignment = Alignment.CenterVertically
          ) {
            UserAvatar(
              photoBase64 = contact?.photo,
              modifier = Modifier.size(40.dp)
            )
            Spacer(modifier = Modifier.width(8.dp))
            Text(
              text = contact?.name ?: peerId,
              maxLines = 1,
              overflow = TextOverflow.Ellipsis
            )
          }
        },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
          }
        },
        actions = {
          IconButton(onClick = { callVM.startVoiceCall(peerId) }) {
            Icon(Icons.Default.Call, contentDescription = "Voice Call")
          }
          IconButton(onClick = { callVM.startVideoCall(peerId) }) {
            Icon(Icons.Default.Videocam, contentDescription = "Video Call")
          }
        }
      )
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
        .navigationBarsPadding()
        .imePadding()
    ) {
      // --- Unified Status Snackbar ---
      val isConnectionIssue = connectionState != ServiceConnectionState.Online &&
          connectionState != ServiceConnectionState.Establishing &&
          connectionState != ServiceConnectionState.Unknown

      val showPermissionBanner = !isConnectionIssue && bannerState != null

      AnimatedVisibility(
        visible = isConnectionIssue || showPermissionBanner,
        enter = expandVertically(expandFrom = Alignment.Top) + fadeIn(),
        exit = shrinkVertically(shrinkTowards = Alignment.Top) + fadeOut()
      ) {
        Box(modifier = Modifier.padding(16.dp)) {
          if (isConnectionIssue) {
            val (msg, isError) = when (connectionState) {
              ServiceConnectionState.NoNetwork -> stringResource(R.string.connection_no_network) to false
              ServiceConnectionState.Failure -> stringResource(R.string.connection_failed) to true
              ServiceConnectionState.Disconnected -> stringResource(R.string.connection_disconnected) to false
              ServiceConnectionState.InvalidToken -> stringResource(R.string.connection_auth_error) to true
              else -> stringResource(R.string.connection_issue) to true
            }
            StatusSnackbar(text = msg, isError = isError)
          } else {
            when (bannerState) {
              BannerType.Notification -> {
                StatusSnackbar(
                  text = "Notifications are disabled",
                  actionLabel = "Fix it",
                  onAction = {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                      notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                    } else {
                      openAppSettings(context)
                    }
                  }
                )
              }

              BannerType.Battery -> {
                StatusSnackbar(
                  text = "Battery optimization may delay messages",
                  actionLabel = "Fix it",
                  onAction = {
                    val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                      data = Uri.fromParts("package", context.packageName, null)
                    }
                    try {
                      context.startActivity(intent)
                    } catch (e: Exception) {
                      openAppSettings(context)
                    }
                  },
                  dismissLabel = "I don't care",
                  onDismiss = { mainViewModel.ignoreBatteryOptimization() }
                )
              }

              BannerType.CallPermissions -> {
                StatusSnackbar(
                  text = "Camera/Mic needed for calls",
                  actionLabel = "Fix it",
                  onAction = {
                    callLauncher.launch(
                      arrayOf(
                        Manifest.permission.CAMERA,
                        Manifest.permission.RECORD_AUDIO,
                        Manifest.permission.READ_PHONE_STATE,
                        Manifest.permission.MANAGE_OWN_CALLS
                      )
                    )
                  },
                  dismissLabel = "I don't care",
                  onDismiss = { mainViewModel.ignoreCallPermissions() }
                )
              }

              null -> {}
            }
          }
        }
      }

      contact?.let {
        ConnectionStatusBar(contact = it)
      }

      val locale = ConfigurationCompat.getLocales(LocalConfiguration.current)[0] ?: Locale.getDefault()
      val zoneId = ZoneId.systemDefault()

      ChatHistory(
        modifier = Modifier.weight(1f),
        historyPage = historyPage,
        listState = listState,
        chatVM = chatVM,
        transferProgress = transferProgress,
        locale = locale,
        zoneId = zoneId,
        onSystemMessageClick = { clickedEntry ->
          if (clickedEntry.isTruncated) {
            tappedTruncatedMessageId = clickedEntry.id
            chatVM.loadFullMessage(clickedEntry.id)
          } else {
            systemMessageToShowDetails = clickedEntry
          }
        },
        onMediaClick = { attachments, index ->
          val target = attachments[index]
          if (target.type == AttachmentType.Image || target.type == AttachmentType.Video) {
            // Visuals: Open internal viewer with only visual items
            val visuals = attachments.filter { it.type == AttachmentType.Image || it.type == AttachmentType.Video }
            val visualIndex = visuals.indexOf(target)
            if (visualIndex != -1) {
              onMediaClick(visuals, visualIndex)
            }
          } else {
            // Files: Open via Intent
            val file = File(target.path)
            if (target.isDownloaded && file.exists()) {
              openFile(context, target.path, target.mime)
            } else {
              Toast.makeText(context, "File not downloaded", Toast.LENGTH_SHORT).show()
            }
          }
        }
      )

      when (contact?.status) {
        ContactStatus.awaitingApproval -> {
          ApprovalRequestBar(
            contact = contact,
            onApprove = { contactsVM.acceptContact(peerId) },
            onReject = {
              contactsVM.blockContact(peerId)
              onBack()
            }
          )
        }

        ContactStatus.pendingApproval -> {
          PendingApprovalBar(
            contact = contact,
            onCancel = {
              contactsVM.blockContact(peerId)
              onBack()
            },
            onResend = { contactsVM.addContact(peerId) }
          )
        }

        else -> {
          MessageInputArea(
            draftAttachments = draftAttachments,
            onSendMessage = chatVM::sendMessage,
            onAddDrafts = chatVM::addDraftAttachments,
            onRemoveDraft = chatVM::removeDraft,
            onUpdateNote = chatVM::updateDraftNote
          )
        }
      }
    }
  }
}

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

  // Camera Photo (Camera returns only one at a time usually)
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

  // Define Authority dynamically based on package name
  val authority = "${context.packageName}.provider"

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
        IconButton(onClick = {
          val tmpFile = File.createTempFile("cam_photo_", ".jpg", context.cacheDir)
          val uri = FileProvider.getUriForFile(context, authority, tmpFile)
          cameraPhotoUri = uri
          takePicture.launch(uri)
        }) {
          Icon(
            Icons.Default.CameraAlt,
            contentDescription = stringResource(R.string.attach_camera_photo),
            tint = MaterialTheme.colorScheme.primary
          )
        }

        // Camera Video
        IconButton(onClick = {
          val tmpFile = File.createTempFile("cam_video_", ".mp4", context.cacheDir)
          val uri = FileProvider.getUriForFile(context, authority, tmpFile)
          cameraVideoUri = uri
          captureVideo.launch(uri)
        }) {
          Icon(
            Icons.Default.Videocam,
            contentDescription = stringResource(R.string.attach_camera_video),
            tint = MaterialTheme.colorScheme.primary
          )
        }

        // Document (Readable docs)
        IconButton(onClick = {
          // OpenMultipleDocuments takes an array of mime types in launch
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

  // Full-Screen Draft Editor / Preview
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
          title = { Text("${pagerState.currentPage + 1} / ${drafts.size}", color = Color.White) },
          navigationIcon = {
            IconButton(onClick = onDismiss) {
              Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back", tint = Color.White)
            }
          },
          actions = {
            IconButton(onClick = { onRemove(pagerState.currentPage) }) {
              Icon(Icons.Default.Delete, contentDescription = "Delete", tint = Color.White)
            }
          },
          colors = TopAppBarDefaults.topAppBarColors(containerColor = Color.Black.copy(alpha = 0.5f))
        )
      }
    ) { padding ->
      Box(modifier = Modifier
        .fillMaxSize()
        .padding(padding)) {
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

        // Note Editor Overlay with Gradient
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
            .padding(bottom = 16.dp) // Extra padding for gesture bar area
        ) {
          TextField(
            value = drafts[pagerState.currentPage].note,
            onValueChange = { onNoteChange(pagerState.currentPage, it) },
            placeholder = { Text(stringResource(R.string.attachment_note_placeholder), color = Color.LightGray) },
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
      Box(modifier = Modifier
        .height(80.dp)
        .fillMaxWidth()) {
        if (draft.bitmap != null) {
          Image(
            bitmap = draft.bitmap.asImageBitmap(),
            contentDescription = null,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxSize()
          )
        } else {
          // File Icon placeholder based on mime type
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

        // Remove button
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

      // Filename (Truncated) + Note
      Column(modifier = Modifier.padding(4.dp)) {
        Text(
          text = draft.name,
          style = MaterialTheme.typography.labelSmall,
          fontWeight = androidx.compose.ui.text.font.FontWeight.Bold,
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

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ConnectionStatusBar(contact: Contact) {
  var isExpanded by remember { mutableStateOf(false) }
  val connectionState = PeerConnectionState.fromInt(contact.connectionState)
  val context = LocalContext.current

  val (statusText, statusColor) = when (connectionState) {
    PeerConnectionState.Connected -> stringResource(R.string.state_peer_connection_connected) to MaterialTheme.extendedColors.success
    PeerConnectionState.Connecting -> stringResource(R.string.state_peer_connection_connecting) to MaterialTheme.colorScheme.secondary
    PeerConnectionState.Failed -> stringResource(R.string.state_peer_connection_failed) to MaterialTheme.colorScheme.error
    else -> stringResource(R.string.state_peer_connection_disconnected) to MaterialTheme.colorScheme.onSurface.copy(
      alpha = 0.6f
    )
  }

  val height by animateDpAsState(
    targetValue = if (isExpanded) 64.dp else 24.dp,
    label = "statusBarHeight"
  )

  Surface(
    modifier = Modifier
      .fillMaxWidth()
      .height(height)
      .combinedClickable(
        onClick = {},
        onLongClick = { isExpanded = !isExpanded }
      ),
    color = MaterialTheme.colorScheme.surfaceContainer
  ) {
    Box(
      modifier = Modifier
        .fillMaxSize()
        .padding(horizontal = 16.dp),
      contentAlignment = Alignment.CenterStart
    ) {
      if (!isExpanded) {
        Row(verticalAlignment = Alignment.CenterVertically) {
          Box(
            modifier = Modifier
              .size(10.dp)
              .background(statusColor, RoundedCornerShape(2.dp))
          )
          Spacer(Modifier.width(8.dp))
          Text(statusText, style = MaterialTheme.typography.labelMedium)
        }
      } else {
        Column(verticalArrangement = Arrangement.Center) {
          Text("Connection Details", style = MaterialTheme.typography.titleSmall)
          Spacer(Modifier.height(4.dp))
          Text(
            "Status: ${connectionState.getResourceString(context)}",
            style = MaterialTheme.typography.bodyMedium
          )
        }
      }
    }
  }
}

@Composable
fun ChatHistory(
  modifier: Modifier = Modifier,
  historyPage: HistoryPage,
  listState: LazyListState,
  chatVM: ConversationViewModel,
  transferProgress: Map<String, Float>,
  locale: Locale,
  zoneId: ZoneId,
  onSystemMessageClick: (HistoryEntry) -> Unit,
  onMediaClick: (List<Attachment>, Int) -> Unit
) {
  val formatters = remember(locale) {
    Triple(
      DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT).withLocale(locale),
      DateTimeFormatter.ofPattern("MMM d, HH:mm", locale),
      DateTimeFormatter.ofLocalizedDateTime(FormatStyle.SHORT, FormatStyle.SHORT).withLocale(locale)
    )
  }

  LazyColumn(
    state = listState,
    modifier = modifier.fillMaxSize(),
    verticalArrangement = Arrangement.spacedBy(2.dp),
    contentPadding = PaddingValues(vertical = 8.dp),
    reverseLayout = true
  ) {
    val history = historyPage.messages
    items(
      count = history.size,
      key = { index ->
        val item = history[history.lastIndex - index]
        if (item.id != 0L) item.id else Long.MIN_VALUE + index
      }
    ) { index ->
      val currentItem = history[history.lastIndex - index]

      val newerItem = if (index > 0) history[history.lastIndex - (index - 1)] else null
      val olderItem = if (index < history.size - 1) history[history.lastIndex - (index + 1)] else null

      val isGroupTail = !shouldGroupMessages(currentItem, newerItem)
      val isGroupHead = !shouldGroupMessages(currentItem, olderItem)

      when (currentItem.kind) {
        HistoryEntryKind.TextMessage -> MessageBubble(
          entry = currentItem,
          onShowMore = chatVM::loadFullMessage,
          transferProgress = transferProgress,
          zoneId = zoneId,
          formatters = formatters,
          onMediaClick = onMediaClick,
          isGroupHead = isGroupHead,
          isGroupTail = isGroupTail,
          onCancelTransfer = { attId -> chatVM.cancelTransfer(attId, currentItem.direction == MessageDirection.Sent) },
          onResumeTransfer = { attId -> chatVM.resumeTransfer(attId, currentItem.direction == MessageDirection.Sent) },
          onDiscardTransfer = { attId -> chatVM.discardAttachment(attId) }
        )

        HistoryEntryKind.VoiceCall, HistoryEntryKind.VideoCall -> CallLogBubble(
          entry = currentItem,
          zoneId = zoneId,
          formatters = formatters
        )

        HistoryEntryKind.SystemMessage -> SystemMessageBubble(currentItem, onClick = onSystemMessageClick)
      }
    }

    if (historyPage.hasOlderMessages) {
      item {
        Button(
          onClick = chatVM::loadOlderMessages,
          modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp)
        ) {
          Text("Load Older Messages")
        }
      }
    }
  }
}

// 5 minutes in microseconds
private const val GROUPING_THRESHOLD_US = 5 * 60 * 1_000_000L

private fun shouldGroupMessages(current: HistoryEntry, other: HistoryEntry?): Boolean {
  if (other == null) return false
  if (current.kind != other.kind) return false
  if (current.kind != HistoryEntryKind.TextMessage) return false
  if (current.direction != other.direction) return false

  val diff = abs(current.timestamp - other.timestamp)
  return diff <= GROUPING_THRESHOLD_US
}

@Composable
fun MessageBubble(
  entry: HistoryEntry,
  onShowMore: (Long) -> Unit,
  transferProgress: Map<String, Float>,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>,
  onMediaClick: (List<Attachment>, Int) -> Unit,
  isGroupHead: Boolean = true,
  isGroupTail: Boolean = true,
  onCancelTransfer: (String) -> Unit,
  onResumeTransfer: (String) -> Unit,
  onDiscardTransfer: (String) -> Unit
) {
  val isFromMe = entry.direction == MessageDirection.Sent
  val alignment = if (isFromMe) Alignment.CenterEnd else Alignment.CenterStart
  val colors = if (isFromMe) {
    MaterialTheme.colorScheme.primaryContainer to MaterialTheme.colorScheme.onPrimaryContainer
  } else {
    MaterialTheme.colorScheme.secondaryContainer to MaterialTheme.colorScheme.onSecondaryContainer
  }

  val topStartRadius = if (!isFromMe && !isGroupHead) 4.dp else 16.dp
  val bottomStartRadius = if (!isFromMe && !isGroupTail) 4.dp else 16.dp
  val topEndRadius = if (isFromMe && !isGroupHead) 4.dp else 16.dp
  val bottomEndRadius = if (isFromMe && !isGroupTail) 4.dp else 16.dp

  val bubbleShape = RoundedCornerShape(
    topStart = topStartRadius,
    topEnd = topEndRadius,
    bottomStart = bottomStartRadius,
    bottomEnd = bottomEndRadius
  )

  val topPadding = if (isGroupHead) 8.dp else 0.dp

  Box(
    modifier = Modifier
      .fillMaxWidth()
      .padding(
        start = if (isFromMe) 48.dp else 8.dp,
        end = if (isFromMe) 8.dp else 48.dp,
        top = topPadding
      ),
    contentAlignment = alignment
  ) {
    Column(
      modifier = Modifier
        .clip(bubbleShape)
        .background(colors.first)
        .padding(horizontal = 4.dp, vertical = 4.dp),
      horizontalAlignment = if (isFromMe) Alignment.End else Alignment.Start
    ) {
      // Render Attachments
      if (entry.attachments.isNotEmpty()) {
        MediaBubbleContent(
          attachments = entry.attachments,
          transferProgress = transferProgress,
          isFromMe = isFromMe,
          onMediaClick = { clickedAttachment ->
            val index = entry.attachments.indexOf(clickedAttachment)
            onMediaClick(entry.attachments, if (index != -1) index else 0)
          },
          onCancel = onCancelTransfer,
          onResume = onResumeTransfer,
          onDiscard = onDiscardTransfer
        )

        if (!entry.content.isNullOrBlank()) {
          Spacer(modifier = Modifier.height(8.dp))
        }
      }

      // Render Text
      if (!entry.content.isNullOrBlank()) {
        Text(
          text = entry.content,
          color = colors.second,
          style = MaterialTheme.typography.bodyLarge,
          modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
        )
        if (entry.isTruncated) {
          TextButton(
            onClick = { onShowMore(entry.id) },
            contentPadding = PaddingValues(top = 8.dp, bottom = 0.dp)
          ) {
            Text("Show more")
          }
        }
      }

      if (isGroupTail) {
        Row(
          modifier = Modifier.padding(top = 4.dp, end = 4.dp, bottom = 4.dp),
          verticalAlignment = Alignment.CenterVertically
        ) {
          Text(
            text = formatTimestamp(entry.timestamp, zoneId, formatters),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
          )
          if (isFromMe) {
            Icon(
              imageVector = when (entry.status) {
                MessageStatus.Read -> Icons.Filled.DoneAll
                MessageStatus.Delivered -> Icons.Filled.Done
                MessageStatus.Sent -> Icons.Filled.Done
                else -> Icons.Filled.Schedule
              },
              contentDescription = "Message status: ${entry.status}",
              modifier = Modifier
                .size(16.dp)
                .padding(start = 4.dp),
              tint = if (entry.status == MessageStatus.Read) {
                MaterialTheme.extendedColors.success
              } else {
                MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
              }
            )
          }
        }
      }
    }
  }
}

@Composable
fun CallLogBubble(
  entry: HistoryEntry,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>
) {
  val (icon, color, text) = when (entry.callDirection) {
    CallDirection.Outgoing -> when (entry.callStatus) {
      CallStatus.Answered -> Triple(
        Icons.AutoMirrored.Filled.CallMade,
        MaterialTheme.colorScheme.primary,
        if (entry.kind == HistoryEntryKind.VideoCall) stringResource(R.string.call_outgoing_video) else stringResource(R.string.call_outgoing_voice)
      )

      else -> Triple(
        Icons.AutoMirrored.Filled.CallMade,
        Color.Red,
        if (entry.kind == HistoryEntryKind.VideoCall) stringResource(R.string.call_outgoing_video) else stringResource(R.string.call_outgoing_voice)
      )
    }

    CallDirection.Incoming -> when (entry.callStatus) {
      CallStatus.Answered -> Triple(
        Icons.AutoMirrored.Filled.CallReceived,
        MaterialTheme.colorScheme.primary,
        if (entry.kind == HistoryEntryKind.VideoCall) stringResource(R.string.call_incoming_video) else stringResource(R.string.call_incoming_voice)
      )

      CallStatus.Missed -> Triple(Icons.AutoMirrored.Filled.CallMissed, Color.Red, stringResource(R.string.call_missed))
      else -> Triple(Icons.AutoMirrored.Filled.CallReceived, Color.Red, stringResource(R.string.call_rejected))
    }

    null -> Triple(
      Icons.Default.Call,
      MaterialTheme.colorScheme.onSurfaceVariant,
      stringResource(R.string.call_generic)
    )
  }

  val formattedTime = formatTimestamp(entry.timestamp, zoneId, formatters)

  val duration = entry.durationSeconds
  val durationText = if (entry.callStatus == CallStatus.Answered && duration > 0) {
    val minutes = TimeUnit.SECONDS.toMinutes(duration.toLong())
    val seconds = duration - TimeUnit.MINUTES.toSeconds(minutes)
    ", $minutes min $seconds sec"
  } else ""

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 8.dp),
    horizontalArrangement = Arrangement.Center,
    verticalAlignment = Alignment.CenterVertically
  ) {
    Icon(
      imageVector = icon,
      contentDescription = text,
      tint = color,
      modifier = Modifier.padding(end = 8.dp)
    )
    Text(
      text = "$text - $formattedTime$durationText",
      style = MaterialTheme.typography.bodySmall,
      color = MaterialTheme.colorScheme.onSurfaceVariant
    )
  }
}

@Composable
fun SystemMessageBubble(entry: HistoryEntry, onClick: (HistoryEntry) -> Unit) {
  val parsedAction = remember(entry.content, entry.isTruncated) {
    entry.content?.let { content ->
      try {
        jsonParser.decodeFromString<JsonAction>(content)
      } catch (e: Exception) {
        val actionMatch = Regex("\"action\"\\s*:\\s*\"([^\"]+)\"").find(content)
        actionMatch?.groupValues?.get(1)?.let { action -> JsonAction(action) }
      }
    }
  }

  // Determine resource ID via helper
  val textResId = LocalizationMapping.getSystemMessageResId(parsedAction?.action)

  // Specific override for confirmation message direction logic
  val finalResId = if (parsedAction?.action == "contact_confirm" && entry.direction == MessageDirection.Sent) {
    textResId
  } else {
    textResId
  }

  if (parsedAction == null && entry.content.isNullOrBlank()) return

  val isClickable = parsedAction?.action == "profile_update_diff"

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 8.dp)
      .clip(MaterialTheme.shapes.small)
      .clickable(enabled = isClickable) { onClick(entry) }
      .padding(horizontal = 16.dp),
    horizontalArrangement = Arrangement.Center
  ) {
    Box(
      modifier = Modifier
        .clip(RoundedCornerShape(12.dp))
        .background(MaterialTheme.colorScheme.surfaceContainer)
        .padding(horizontal = 12.dp, vertical = 6.dp)
    ) {
      Text(
        text = stringResource(id = finalResId),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
    }
  }
}

@Serializable
private data class JsonAction(val action: String)

@Composable
private fun PendingApprovalBar(
  contact: Contact,
  onCancel: () -> Unit,
  onResend: () -> Unit
) {
  Surface(
    modifier = Modifier.fillMaxWidth(),
    tonalElevation = 3.dp
  ) {
    Column(
      modifier = Modifier
        .padding(16.dp)
        .navigationBarsPadding(),
      horizontalAlignment = Alignment.CenterHorizontally
    ) {
      Text(
        text = "Waiting for ${contact.name} to approve your request.",
        style = MaterialTheme.typography.titleMedium,
        textAlign = TextAlign.Center
      )
      Spacer(modifier = Modifier.height(16.dp))
      Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly
      ) {
        OutlinedButton(onClick = onCancel) { Text("Cancel Request") }
        Button(onClick = onResend) { Text("Resend Request") }
      }
    }
  }
}

@Composable
private fun ApprovalRequestBar(
  contact: Contact,
  onApprove: () -> Unit,
  onReject: () -> Unit
) {
  Surface(
    modifier = Modifier.fillMaxWidth(),
    tonalElevation = 3.dp
  ) {
    Column(
      modifier = Modifier
        .padding(16.dp)
        .navigationBarsPadding(),
      horizontalAlignment = Alignment.CenterHorizontally
    ) {
      Text(
        text = "${contact.name} would like to connect.",
        style = MaterialTheme.typography.titleMedium,
        textAlign = TextAlign.Center
      )
      if (!contact.bio.isNullOrBlank()) {
        Spacer(modifier = Modifier.height(8.dp))
        Text(
          text = "\"${contact.bio}\"",
          style = MaterialTheme.typography.bodyMedium,
          fontStyle = FontStyle.Italic,
          textAlign = TextAlign.Center,
          color = MaterialTheme.colorScheme.onSurfaceVariant
        )
      }
      Spacer(modifier = Modifier.height(16.dp))
      Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly
      ) {
        OutlinedButton(onClick = onReject) { Text("Reject") }
        Button(onClick = onApprove) { Text("Approve Contact") }
      }
    }
  }
}

fun formatTimestamp(
  timestamp: Long,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>
): String {
  val messageTime = Instant.ofEpochMilli(timestamp / 1000).atZone(zoneId)
  val now = ZonedDateTime.now(zoneId)

  val isToday = messageTime.toLocalDate().isEqual(now.toLocalDate())
  val isYesterday = messageTime.toLocalDate().isEqual(now.toLocalDate().minusDays(1))

  return when {
    isToday -> messageTime.format(formatters.first)
    isYesterday -> "Yesterday, " + messageTime.format(formatters.first)
    messageTime.year == now.year -> messageTime.format(formatters.second)
    else -> messageTime.format(formatters.third)
  }
}

private fun openFile(context: Context, path: String, mimeType: String) {
  val file = File(path)
  if (!file.exists()) return
  val uri = FileProvider.getUriForFile(context, "${context.packageName}.provider", file)
  val intent = Intent(Intent.ACTION_VIEW).apply {
    setDataAndType(uri, mimeType)
    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
  }
  try {
    context.startActivity(intent)
  } catch (e: ActivityNotFoundException) {
    Toast.makeText(context, "No app found to open this file", Toast.LENGTH_SHORT).show()
  }
}

private fun openAppSettings(context: Context) {
  val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
    data = Uri.fromParts("package", context.packageName, null)
    flags = Intent.FLAG_ACTIVITY_NEW_TASK
  }
  context.startActivity(intent)
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
