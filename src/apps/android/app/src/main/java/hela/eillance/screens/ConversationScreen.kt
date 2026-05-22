package hela.eillance.screens

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.SoundPool
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SmallFloatingActionButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.ClipEntry
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import androidx.core.os.ConfigurationCompat
import hela.eillance.MainViewModel
import hela.eillance.SharedContent
import hela.eillance.camera.R
import hela.eillance.components.GlobalStatusBanner
import hela.eillance.components.StatusSnackbar
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.screens.conversation.ApprovalRequestBar
import hela.eillance.screens.conversation.ChatHistory
import hela.eillance.screens.conversation.ConnectionStatusBar
import hela.eillance.screens.conversation.ConversationDefaultTopBar
import hela.eillance.screens.conversation.DeleteMessageDialog
import hela.eillance.screens.conversation.MessageInputArea
import hela.eillance.screens.conversation.PendingApprovalBar
import hela.eillance.screens.conversation.SelectionTopBar
import hela.eillance.screens.conversation.formatTimestamp
import hela.eillance.utils.shareContent
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import hela.eillance.viewmodels.SettingsViewModel
import kotlinx.coroutines.launch
import java.io.File
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import kotlin.math.abs

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConversationScreen(
  chatVM: ConversationViewModel,
  contactsVM: ContactsViewModel,
  settingsVM: SettingsViewModel,
  callVM: CallViewModel,
  targetMessageId: Long?,
  mainViewModel: MainViewModel,
  peerId: String,
  contact: Contact?,
  myId: String,
  historyPage: HistoryPage,
  callState: CallState,
  connectionState: ServiceConnectionState,
  onBack: () -> Unit,
  onNavigateToCall: (String) -> Unit,
  onShowContactProfile: (String) -> Unit,
  onNavigateToMedia: () -> Unit,
  onMediaClick: (List<Attachment>, Int) -> Unit
) {
  val listState = rememberLazyListState()
  val coroutineScope = rememberCoroutineScope()
  val transferProgress by chatVM.transferProgress.collectAsState()
  val draftAttachments by chatVM.draftAttachments.collectAsState()
  val messageText by chatVM.messageText.collectAsState()
  val peerNeedsUpdate by chatVM.peerNeedsUpdate.collectAsState()
  val weNeedUpdate by chatVM.weNeedUpdate.collectAsState()
  val context = LocalContext.current
  val playSoundOnSend by settingsVM.playSoundOnSend.collectAsState()
  val playSoundOnReceive by settingsVM.playSoundOnReceive.collectAsState()

  val bannerState by mainViewModel.permissionBannerState.collectAsState()
  val clipboard = LocalClipboard.current
  var scrollToMessageId by remember { mutableStateOf(targetMessageId) }
  val isCoreUnresponsive by mainViewModel.isCoreUnresponsive.collectAsState()

  // Selection State
  var selectedMessageIds by remember { mutableStateOf(emptySet<Long>()) }
  val selectionMode = selectedMessageIds.isNotEmpty()

  // Search State
  var isSearchActive by rememberSaveable { mutableStateOf(false) }
  var searchQuery by rememberSaveable { mutableStateOf("") }
  val searchResults by chatVM.searchResults.collectAsState()
  val focusRequester = remember { FocusRequester() }
  val focusManager = LocalFocusManager.current

  // Message Actions State
  // Note: activeMessageActionId determines which message shows the POPUP menu.
  // editingMessageId determines which message shows the IN-PLACE editor.
  var activeMessageActionId by remember { mutableStateOf<Long?>(null) }
  var editingMessageId by remember { mutableStateOf<Long?>(null) }
  var replyingToMessage by remember { mutableStateOf<HistoryEntry?>(null) }
  var showDeleteDialogId by remember { mutableStateOf<Long?>(null) }

  var isInputFocused by remember { mutableStateOf(false) }
  var lastMenuDismissTime by remember { mutableLongStateOf(0L) }

  // Scroll to bottom button visibility (Reverse layout: index 0 is bottom)
  val showScrollToBottom by remember {
    derivedStateOf { listState.firstVisibleItemIndex > 5 }
  }

  // Handle Back press to clear selection or search or active action
  BackHandler(enabled = selectionMode || isSearchActive || activeMessageActionId != null || editingMessageId != null || replyingToMessage != null) {
    if (activeMessageActionId != null) {
      activeMessageActionId = null
    } else if (editingMessageId != null) {
      editingMessageId = null
    } else if (replyingToMessage != null) {
      replyingToMessage = null
    } else if (selectionMode) {
      selectedMessageIds = emptySet()
    } else {
      isSearchActive = false
      searchQuery = ""
      chatVM.search(searchQuery)
    }
  }

  LaunchedEffect(isSearchActive) {
    if (isSearchActive) focusRequester.requestFocus() else focusManager.clearFocus()
  }

  val locale = ConfigurationCompat.getLocales(LocalConfiguration.current)[0] ?: Locale.getDefault()
  val zoneId = ZoneId.systemDefault()
  val formatters = remember(locale) {
    Triple(
      DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT).withLocale(locale),
      DateTimeFormatter.ofPattern("MMM d, HH:mm", locale),
      DateTimeFormatter.ofLocalizedDateTime(FormatStyle.SHORT, FormatStyle.SHORT).withLocale(locale)
    )
  }

  // State for Call Action permissions
  val requestVoiceCallPermission = rememberLauncherForActivityResult(
    ActivityResultContracts.RequestPermission()
  ) { isGranted ->
    if (isGranted) {
      callVM.startVoiceCall(peerId)
    } else {
      Toast.makeText(context, R.string.call_perm_mic_required, Toast.LENGTH_SHORT).show()
    }
  }

  val requestVideoCallPermission = rememberLauncherForActivityResult(
    ActivityResultContracts.RequestMultiplePermissions()
  ) { permissions ->
    val cam = permissions[Manifest.permission.CAMERA] ?: false
    val mic = permissions[Manifest.permission.RECORD_AUDIO] ?: false
    if (cam && mic) {
      callVM.startVideoCall(peerId)
    } else {
      Toast.makeText(context, R.string.call_perm_cam_mic_required, Toast.LENGTH_SHORT).show()
    }
  }

  // --- Sound Effects Setup ---
  val soundPool = remember {
    SoundPool.Builder()
      .setMaxStreams(1)
      .setAudioAttributes(
        AudioAttributes.Builder()
          .setUsage(AudioAttributes.USAGE_MEDIA) // Use MEDIA for in-app sounds to ensure audibility
          .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
          .build()
      )
      .build()
  }
  val sentSoundId = remember { mutableIntStateOf(0) }
  val receivedSoundId = remember { mutableIntStateOf(0) }

  LaunchedEffect(Unit) {
    sentSoundId.intValue = soundPool.load(context, R.raw.message_sent, 1)
    receivedSoundId.intValue = soundPool.load(context, R.raw.message_received, 1)
  }

  DisposableEffect(Unit) {
    onDispose { soundPool.release() }
  }

  var newestMessageId by remember(peerId) { mutableStateOf(historyPage.messages.lastOrNull()?.id) }

  var systemMessageToShowDetails by remember { mutableStateOf<HistoryEntry?>(null) }
  var tappedTruncatedMessageId by remember { mutableStateOf<Long?>(null) }

  LaunchedEffect(targetMessageId) {
    targetMessageId?.let { msgId ->
      chatVM.loadUpToMessage(msgId)
      scrollToMessageId = msgId
    }
  }

  // Handle Shared Content
  LaunchedEffect(peerId) {
    val pendingShare = mainViewModel.consumePendingShareContent()
    if (pendingShare != null) {
      if (!pendingShare.text.isNullOrBlank()) {
        chatVM.updateMessageText(pendingShare.text)
      }
      if (pendingShare.uris.isNotEmpty()) {
        chatVM.addDraftAttachments(pendingShare.uris)
      }
    }
  }

  // Detect new messages for Sound and Auto-Scroll
  LaunchedEffect(historyPage.messages) {
    val newMessages = historyPage.messages
    val newLastMsg = newMessages.lastOrNull()

    // If the newest message ID changed, it means a new message was added (sent or received)
    // ignoring cases where we just loaded older history (pagination).
    if (newLastMsg != null && newLastMsg.id != newestMessageId) {
      // Play Sound
      val msgTime = newLastMsg.timestamp / 1000
      val now = System.currentTimeMillis()
      val isRecent = abs(now - msgTime) < 2000

      if (isRecent) {
        if (newLastMsg.direction == MessageDirection.Sent) {
          if (sentSoundId.intValue != 0 && playSoundOnSend) {
            soundPool.play(sentSoundId.intValue, 1f, 1f, 0, 0, 1f)
          }
        } else {
          if (receivedSoundId.intValue != 0 && playSoundOnReceive) {
            soundPool.play(receivedSoundId.intValue, 1f, 1f, 0, 0, 1f)
          }
        }
      }

      // Scroll to bottom if already near bottom
      if (!isSearchActive && listState.firstVisibleItemIndex < 3) {
        coroutineScope.launch {
          listState.animateScrollToItem(0)
        }
      }

      newestMessageId = newLastMsg.id
    }
  }

  LaunchedEffect(scrollToMessageId, historyPage) {
    scrollToMessageId?.let { targetId ->
      // Wait for composition to settle if data just updated
      kotlinx.coroutines.delay(50)
      val historyIndex = historyPage.messages.indexOfFirst { it.id == targetId }
      if (historyIndex != -1) {
        val lazyColumnIndex = historyPage.messages.lastIndex - historyIndex
        // Ensure index is valid for the current list state
        if (lazyColumnIndex in 0 until historyPage.messages.size) {
          listState.scrollToItem(lazyColumnIndex)
          scrollToMessageId = null
        }
      }
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

  showDeleteDialogId?.let { msgId ->
    DeleteMessageDialog(
      onDismiss = { showDeleteDialogId = null },
      onConfirm = {
        chatVM.deleteMessage(msgId)
        showDeleteDialogId = null
      }
    )
  }

  Scaffold(
    topBar = {
      if (isSearchActive) {
        TopAppBar(
          title = {
            TextField(
              value = searchQuery,
              onValueChange = {
                searchQuery = it
                chatVM.search(it)
              },
              placeholder = { Text(stringResource(R.string.search_hint)) },
              modifier = Modifier
                .fillMaxWidth()
                .focusRequester(focusRequester),
              colors = TextFieldDefaults.colors(
                focusedContainerColor = Color.Transparent,
                unfocusedContainerColor = Color.Transparent,
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent
              ),
              singleLine = true,
              keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
              keyboardActions = KeyboardActions(onSearch = { focusManager.clearFocus() })
            )
          },
          navigationIcon = {
            IconButton(onClick = {
              isSearchActive = false
              searchQuery = ""
              chatVM.search("")
            }) {
              Icon(
                Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = stringResource(R.string.action_back)
              )
            }
          },
          actions = {
            if (searchQuery.isNotEmpty()) {
              IconButton(onClick = {
                searchQuery = ""
                chatVM.search("")
                focusRequester.requestFocus()
              }) {
                Icon(
                  Icons.Default.Close,
                  contentDescription = stringResource(R.string.action_close)
                )
              }
            }
          },
          colors = TopAppBarDefaults.topAppBarColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainer
          )
        )
      } else if (selectionMode) {
        SelectionTopBar(
          count = selectedMessageIds.size,
          onClear = { selectedMessageIds = emptySet() },
          onCopy = {
            val selectedMsgs = historyPage.messages
              .filter { it.id in selectedMessageIds }
              .sortedBy { it.timestamp }

            if (selectedMsgs.isNotEmpty()) {
              val directions = selectedMsgs.map { it.direction }.toSet()
              val isConversation = directions.size > 1
              val sb = StringBuilder()

              val dateTimeFormatter =
                DateTimeFormatter.ofLocalizedDateTime(FormatStyle.SHORT, FormatStyle.SHORT)
                  .withLocale(locale)
                  .withZone(zoneId)

              selectedMsgs.forEach { msg ->
                val content = msg.content
                if (!content.isNullOrBlank()) {
                  if (sb.isNotEmpty()) sb.append("\n")

                  if (isConversation) {
                    val name = contact?.name ?: peerId
                    val time =
                      dateTimeFormatter.format(Instant.ofEpochMilli(msg.timestamp / 1000))
                    sb.append("[$time] $name: $content")
                  } else {
                    sb.append(content)
                  }
                }
              }

              if (sb.isNotEmpty()) {
                coroutineScope.launch {
                  val clipData = ClipData.newPlainText("Mercury Chat", sb.toString())
                  clipboard.setClipEntry(ClipEntry(clipData))
                }
              }
            }
            selectedMessageIds = emptySet()
          }
        )
      } else {
        ConversationDefaultTopBar(
          peerId = peerId,
          contact = contact,
          onBack = onBack,
          onShowContactProfile = onShowContactProfile,
          onStartVoiceCall = {
            if (ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.RECORD_AUDIO
              ) == PackageManager.PERMISSION_GRANTED
            ) {
              callVM.startVoiceCall(peerId)
            } else {
              requestVoiceCallPermission.launch(Manifest.permission.RECORD_AUDIO)
            }
          },
          onStartVideoCall = {
            val cam = ContextCompat.checkSelfPermission(
              context,
              Manifest.permission.CAMERA
            ) == PackageManager.PERMISSION_GRANTED
            val mic = ContextCompat.checkSelfPermission(
              context,
              Manifest.permission.RECORD_AUDIO
            ) == PackageManager.PERMISSION_GRANTED
            if (cam && mic) {
              callVM.startVideoCall(peerId)
            } else {
              requestVideoCallPermission.launch(
                arrayOf(Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO)
              )
            }
          },
          onMediaClick = onNavigateToMedia,
          onSearchClick = { isSearchActive = true }
        )
      }
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
        .navigationBarsPadding()
        .imePadding()
    ) {
      androidx.compose.animation.AnimatedVisibility(visible = historyPage.isSyncing) {
        if (historyPage.syncProgress < 0f) {
          androidx.compose.material3.LinearProgressIndicator(
            modifier = Modifier.fillMaxWidth(),
            color = MaterialTheme.colorScheme.primary,
            trackColor = Color.Transparent
          )
        } else {
          androidx.compose.material3.LinearProgressIndicator(
            progress = { historyPage.syncProgress },
            modifier = Modifier.fillMaxWidth(),
            color = MaterialTheme.colorScheme.primary,
            trackColor = Color.Transparent
          )
        }
      }

      if (isSearchActive) {
        if (searchResults.isEmpty() && searchQuery.isNotEmpty()) {
          Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.search_no_results))
          }
        } else {
          LazyColumn(modifier = Modifier.weight(1f)) {
            items(searchResults) { msg ->
              val isFromMe = msg.direction == MessageDirection.Sent
              Column(
                modifier = Modifier
                  .fillMaxWidth()
                  .clickable {
                    isSearchActive = false
                    searchQuery = ""
                    chatVM.search("")
                    chatVM.loadUpToMessage(msg.id)
                    scrollToMessageId = msg.id
                  }
                  .padding(16.dp)
              ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                  Text(
                    text = if (isFromMe) stringResource(R.string.me) else contact?.name ?: peerId,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = FontWeight.Bold,
                    color = if (isFromMe) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
                  )
                  Text(
                    text = " * " + formatTimestamp(msg.timestamp, zoneId, formatters),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(start = 8.dp)
                  )
                }
                Text(
                  text = msg.content ?: "",
                  style = MaterialTheme.typography.bodyMedium,
                  modifier = Modifier.padding(top = 4.dp)
                )
              }
              HorizontalDivider(color = DividerDefaults.color.copy(alpha = 0.5f))
            }
          }
        }
      } else {
        val isConnectionIssue = connectionState != ServiceConnectionState.Online &&
            connectionState != ServiceConnectionState.Establishing &&
            connectionState != ServiceConnectionState.Unknown

        GlobalStatusBanner(
          isCoreUnresponsive = isCoreUnresponsive,
          connectionState = connectionState,
          bannerState = bannerState,
          onResetCore = { mainViewModel.resetCore() },
          onCheckPermissions = { mainViewModel.checkAllPermissions() },
      onIgnoreBattery = { mainViewModel.ignoreBatteryOptimization() },
      onIgnoreCalls = { mainViewModel.ignoreCallPermissions() }
    )

    if (weNeedUpdate) {
      StatusSnackbar(
        text = stringResource(R.string.version_we_need_update),
        isError = true
      )
    } else if (peerNeedsUpdate) {
      StatusSnackbar(
        text = stringResource(R.string.version_peer_needs_update),
            isError = true
          )
        }
        
        if (historyPage.isUnreachable) {
          StatusSnackbar(
            text = stringResource(R.string.contact_unreachable),
            actionLabel = stringResource(R.string.action_retry),
            onAction = { chatVM.retrySync() },
            isError = true
          )
        } else if (historyPage.isUnwilling) {
          StatusSnackbar(
            text = stringResource(R.string.contact_unwilling),
            actionLabel = stringResource(R.string.action_retry),
            onAction = { chatVM.retrySync() },
            isError = true
          )
        }

        contact?.let {
          ConnectionStatusBar(contact = it)
        }

        Box(
          modifier = Modifier
            .weight(1f)
            .pointerInput(Unit) {
              detectTapGestures(onTap = {
                if (isInputFocused) focusManager.clearFocus()
                if (activeMessageActionId != null) {
                  activeMessageActionId = null
                  lastMenuDismissTime = System.currentTimeMillis()
                }
              })
            }
        ) {
          ChatHistory(
            modifier = Modifier.fillMaxSize(),
            historyPage = historyPage,
            listState = listState,
            chatVM = chatVM,
            transferProgress = transferProgress,
            locale = locale,
            zoneId = zoneId,
            selectedMessageIds = selectedMessageIds,
            selectionMode = selectionMode,
            activeMessageActionId = activeMessageActionId,
            editingMessageId = editingMessageId,
            myId = myId,
            contactName = contact?.displayName ?: peerId.take(8),
            onSwipeToReply = { replyingToMessage = it },
            onReplyClick = { scrollToMessageId = it },
            onToggleSelection = { id ->
              if (isInputFocused) focusManager.clearFocus()
              if (activeMessageActionId != null) {
                activeMessageActionId = null
                lastMenuDismissTime = System.currentTimeMillis()
              }
              selectedMessageIds = if (id in selectedMessageIds) {
                selectedMessageIds - id
              } else {
                selectedMessageIds + id
              }
            },
            onSystemMessageClick = { clickedEntry ->
              if (isInputFocused) {
                focusManager.clearFocus()
              } else if (activeMessageActionId != null) {
                activeMessageActionId = null
                lastMenuDismissTime = System.currentTimeMillis()
              } else {
                if (clickedEntry.isTruncated) {
                  tappedTruncatedMessageId = clickedEntry.id
                  chatVM.loadFullMessage(clickedEntry.id)
                } else {
                  systemMessageToShowDetails = clickedEntry
                }
              }
            },
            onMediaClick = { attachments, index ->
              if (isInputFocused) {
                focusManager.clearFocus()
              } else if (activeMessageActionId != null) {
                activeMessageActionId = null
                lastMenuDismissTime = System.currentTimeMillis()
              } else {
                val target = attachments[index]
                if (target.type == AttachmentType.Image || target.type == AttachmentType.Video) {
                  val visuals =
                    attachments.filter { it.type == AttachmentType.Image || it.type == AttachmentType.Video }
                  val visualIndex = visuals.indexOf(target)
                  if (visualIndex != -1) {
                    onMediaClick(visuals, visualIndex)
                  }
                } else {
                  val file = File(target.path)
                  if (target.isDownloaded && file.exists()) {
                    openFile(context, file, target.mime)
                  } else {
                    Toast.makeText(
                      context,
                      R.string.toast_file_not_downloaded,
                      Toast.LENGTH_SHORT
                    ).show()
                  }
                }
              }
            },
            onMessageClick = { entry ->
              if (isInputFocused) {
                focusManager.clearFocus()
              } else if (activeMessageActionId != null) {
                activeMessageActionId = null
                lastMenuDismissTime = System.currentTimeMillis()
              } else if (System.currentTimeMillis() - lastMenuDismissTime > 300L) {
                activeMessageActionId = entry.id
              }
            },
            onDismissMenu = {
              activeMessageActionId = null
              lastMenuDismissTime = System.currentTimeMillis()
            },
            onActionReact = { msgId, reaction ->
              chatVM.reactToMessage(msgId, reaction)
              activeMessageActionId = null
              lastMenuDismissTime = System.currentTimeMillis()
            },
            onActionCopy = { content ->
              coroutineScope.launch {
                val clip = ClipData.newPlainText("Message", content)
                clipboard.setClipEntry(ClipEntry(clip))
              }
              activeMessageActionId = null
              lastMenuDismissTime = System.currentTimeMillis()
            },
            onActionShare = { entry ->
              shareContent(context, entry.content, entry.attachments)
            },
            onActionForward = { entry ->
              val uris = entry.attachments.filter { it.isDownloaded }.map {
                FileProvider.getUriForFile(context, "${context.packageName}.provider", File(it.path))
              }
              mainViewModel.setPendingShare(SharedContent(text = entry.content, uris = uris))
            },
            onActionEdit = { msgId ->
              activeMessageActionId = null
              lastMenuDismissTime = System.currentTimeMillis()
              editingMessageId = msgId
            },
            onActionDelete = { msgId ->
              activeMessageActionId = null
              lastMenuDismissTime = System.currentTimeMillis()
              showDeleteDialogId = msgId
            },
            onEditCommit = { msgId, newText ->
              chatVM.editMessage(msgId, newText)
              editingMessageId = null
            },
            onEditCancel = {
              editingMessageId = null
            }
          )

          androidx.compose.animation.AnimatedVisibility(
            visible = showScrollToBottom,
            modifier = Modifier
              .align(Alignment.BottomEnd)
              .padding(16.dp),
            enter = fadeIn() + scaleIn(),
            exit = fadeOut() + scaleOut()
          ) {
            SmallFloatingActionButton(
              onClick = {
                coroutineScope.launch {
                  listState.animateScrollToItem(0)
                }
              },
              containerColor = MaterialTheme.colorScheme.secondaryContainer,
              contentColor = MaterialTheme.colorScheme.onSecondaryContainer
            ) {
              Icon(
                Icons.Default.KeyboardArrowDown,
                contentDescription = stringResource(R.string.action_scroll_to_bottom)
              )
            }
          }
        }

        when (contact?.status) {
          ContactStatus.awaitingApproval -> {
            ApprovalRequestBar(
              contact = contact,
              onApprove = { contactsVM.acceptContact(peerId) },
              onReject = {
                contactsVM.blockContact(peerId)
              }
            )
          }

          ContactStatus.pendingApproval -> {
            PendingApprovalBar(
              contact = contact,
              onCancel = {
                contactsVM.blockContact(peerId)
              },
              onResend = { contactsVM.addContact(peerId) }
            )
          }

          else -> {
            MessageInputArea(
              inputValue = messageText,
              onInputValueChange = chatVM::updateMessageText,
              draftAttachments = draftAttachments,
              isFocused = isInputFocused,
              onFocusChange = { focused ->
                isInputFocused = focused
                if (focused && activeMessageActionId != null) {
                  activeMessageActionId = null
                  lastMenuDismissTime = System.currentTimeMillis()
                }
              },
              replyingToMessage = replyingToMessage,
              replyingToName = if (replyingToMessage?.direction == MessageDirection.Sent) stringResource(R.string.me) else (contact?.displayName ?: peerId.take(8)),
              onCancelReply = { replyingToMessage = null },
              onSendMessage = { text ->
                chatVM.sendMessage(text, replyingToMessage?.id)
                replyingToMessage = null
                coroutineScope.launch {
                  listState.animateScrollToItem(0)
                }
              },
              onAddDrafts = chatVM::addDraftAttachments,
              onRemoveDraft = chatVM::removeDraft,
              onUpdateNote = chatVM::updateDraftNote
            )
          }
        }
      }
    }
  }
}

private fun openFile(context: Context, file: File, mimeType: String) {
  if (!file.exists()) return
  val uri = FileProvider.getUriForFile(context, "${context.packageName}.provider", file)
  val intent = Intent(Intent.ACTION_VIEW).apply {
    setDataAndType(uri, mimeType)
    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
  }
  try {
    context.startActivity(intent)
  } catch (_: ActivityNotFoundException) {
    Toast.makeText(context, context.getString(R.string.toast_no_app_for_file), Toast.LENGTH_SHORT)
      .show()
  }
}
