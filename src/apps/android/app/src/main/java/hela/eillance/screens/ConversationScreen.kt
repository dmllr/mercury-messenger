package hela.eillance.screens

import android.Manifest
import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.widget.Toast
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material3.Scaffold
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
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.core.os.ConfigurationCompat
import hela.eillance.BannerType
import hela.eillance.MainViewModel
import hela.eillance.camera.R
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
import hela.eillance.screens.conversation.MessageInputArea
import hela.eillance.screens.conversation.PendingApprovalBar
import hela.eillance.screens.conversation.SelectionTopBar
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import kotlinx.coroutines.launch
import java.io.File
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

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
  val messageText by chatVM.messageText.collectAsState()
  val context = LocalContext.current
  val bannerState by mainViewModel.permissionBannerState.collectAsState()
  val clipboardManager = LocalClipboardManager.current

  // Selection State
  var selectedMessageIds by remember { mutableStateOf(emptySet<Long>()) }
  val selectionMode = selectedMessageIds.isNotEmpty()

  // Handle Back press to clear selection
  BackHandler(enabled = selectionMode) {
    selectedMessageIds = emptySet()
  }

  val locale = ConfigurationCompat.getLocales(LocalConfiguration.current)[0] ?: Locale.getDefault()
  val zoneId = ZoneId.systemDefault()

  // State for permissions launchers
  val notifLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { mainViewModel.checkAllPermissions() }
  val callLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { mainViewModel.checkAllPermissions() }

  var previousHistorySize by remember(peerId) { mutableIntStateOf(historyPage.messages.size) }

  var systemMessageToShowDetails by remember { mutableStateOf<HistoryEntry?>(null) }
  var tappedTruncatedMessageId by remember { mutableStateOf<Long?>(null) }

  // Auto-navigation to call screen
  LaunchedEffect(
    callState.isConnecting,
    callState.isRinging,
    callState.isInCall,
    callState.peerId
  ) {
    if ((callState.isConnecting || callState.isRinging || callState.isInCall) && callState.peerId == peerId) {
      onNavigateToCall(peerId)
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
      if (selectionMode) {
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
                    val name = if (msg.direction == MessageDirection.Sent) {
                      context.getString(R.string.chat_you_prefix).trimEnd(' ', ':')
                    } else {
                      contact?.name ?: peerId
                    }
                    val time =
                      dateTimeFormatter.format(Instant.ofEpochMilli(msg.timestamp / 1000))
                    sb.append("[$time] $name: $content")
                  } else {
                    sb.append(content)
                  }
                }
              }

              if (sb.isNotEmpty()) {
                clipboardManager.setText(AnnotatedString(sb.toString()))
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
          onStartVoiceCall = { callVM.startVoiceCall(peerId) },
          onStartVideoCall = { callVM.startVideoCall(peerId) }
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
                  text = stringResource(id = R.string.permission_banner_notifications),
                  actionLabel = stringResource(id = R.string.action_fix_it),
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
                  text = stringResource(id = R.string.permission_banner_battery),
                  actionLabel = "Fix it",
                  onAction = {
                    val intent =
                      Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                        data = Uri.fromParts("package", context.packageName, null)
                      }
                    try {
                      context.startActivity(intent)
                    } catch (e: Exception) {
                      openAppSettings(context)
                    }
                  },
                  dismissLabel = stringResource(id = R.string.action_ignore_permission),
                  onDismiss = { mainViewModel.ignoreBatteryOptimization() }
                )
              }

              BannerType.CallPermissions -> {
                StatusSnackbar(
                  text = stringResource(id = R.string.permission_banner_calls),
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
                  dismissLabel = stringResource(id = R.string.action_ignore_permission),
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

      ChatHistory(
        modifier = Modifier.weight(1f),
        historyPage = historyPage,
        listState = listState,
        chatVM = chatVM,
        transferProgress = transferProgress,
        locale = locale,
        zoneId = zoneId,
        selectedMessageIds = selectedMessageIds,
        selectionMode = selectionMode,
        onToggleSelection = { id ->
          selectedMessageIds = if (id in selectedMessageIds) {
            selectedMessageIds - id
          } else {
            selectedMessageIds + id
          }
        },
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
            inputValue = messageText,
            onInputValueChange = chatVM::updateMessageText,
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

private fun openFile(context: Context, file: File, mimeType: String) {
  if (!file.exists()) return
  val uri = FileProvider.getUriForFile(context, "${context.packageName}.provider", file)
  val intent = Intent(Intent.ACTION_VIEW).apply {
    setDataAndType(uri, mimeType)
    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
  }
  try {
    context.startActivity(intent)
  } catch (e: ActivityNotFoundException) {
    Toast.makeText(context, context.getString(R.string.toast_no_app_for_file), Toast.LENGTH_SHORT)
      .show()
  }
}

private fun openAppSettings(context: Context) {
  val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
    data = Uri.fromParts("package", context.packageName, null)
    flags = Intent.FLAG_ACTIVITY_NEW_TASK
  }
  context.startActivity(intent)
}
