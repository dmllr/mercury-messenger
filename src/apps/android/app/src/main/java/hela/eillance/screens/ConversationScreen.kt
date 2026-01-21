package hela.eillance.screens

import androidx.compose.animation.core.animateDpAsState
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
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.CallMade
import androidx.compose.material.icons.automirrored.filled.CallMissed
import androidx.compose.material.icons.automirrored.filled.CallReceived
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.DoneAll
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Schedule
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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.os.ConfigurationCompat
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
import hela.eillance.theme.extendedColors
import hela.eillance.utils.rememberImageBitmapFromBase64
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.ConversationViewModel
import kotlinx.coroutines.launch
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import java.util.concurrent.TimeUnit

private val jsonParser = Json { ignoreUnknownKeys = true }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConversationScreen(
  chatVM: ConversationViewModel,
  contactsVM: ContactsViewModel,
  callVM: CallViewModel,
  peerId: String,
  contact: Contact?,
  historyPage: HistoryPage,
  callState: CallState,
  hasCallPermissions: Boolean,
  hasNotificationPermission: Boolean,
  onCheckPermissions: () -> Unit,
  onBack: () -> Unit,
  onNavigateToCall: (String) -> Unit,
  onShowContactProfile: (String) -> Unit
) {
  val listState = rememberLazyListState()
  val coroutineScope = rememberCoroutineScope()

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
            val imageBitmap = rememberImageBitmapFromBase64(contact?.photo)
            if (imageBitmap != null) {
              Image(
                bitmap = imageBitmap,
                contentDescription = "Profile Photo",
                modifier = Modifier
                  .size(40.dp)
                  .clip(CircleShape),
              )
            } else {
              Icon(
                imageVector = Icons.Default.Person,
                contentDescription = "Default Profile Icon",
                modifier = Modifier
                  .size(40.dp)
                  .clip(CircleShape)
                  .background(MaterialTheme.colorScheme.secondaryContainer)
                  .padding(8.dp)
              )
            }
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
      if (!hasNotificationPermission) {
        NotificationPermissionBanner(onCheckPermissions)
      }
      if (!hasCallPermissions) {
        CallPermissionBanner(onCheckPermissions)
      }

      contact?.let {
        ConnectionStatusBar(contact = it)
      }

      // Capture locale and time zone once for the list
      val locale = ConfigurationCompat.getLocales(LocalConfiguration.current)[0] ?: Locale.getDefault()
      val zoneId = ZoneId.systemDefault()

      ChatHistory(
        modifier = Modifier.weight(1f),
        historyPage = historyPage,
        listState = listState,
        chatVM = chatVM,
        locale = locale,
        zoneId = zoneId,
        onSystemMessageClick = { clickedEntry ->
          if (clickedEntry.isTruncated) {
            tappedTruncatedMessageId = clickedEntry.id
            chatVM.loadFullMessage(clickedEntry.id)
          } else {
            systemMessageToShowDetails = clickedEntry
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
          MessageInput(onSendMessage = chatVM::sendMessage)
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
    PeerConnectionState.Connected -> "Connected" to MaterialTheme.extendedColors.success
    PeerConnectionState.Connecting -> "Connecting..." to MaterialTheme.colorScheme.secondary
    PeerConnectionState.Failed -> "Connection Failed" to MaterialTheme.colorScheme.error
    else -> "Offline" to MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f)
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
  locale: Locale,
  zoneId: ZoneId,
  onSystemMessageClick: (HistoryEntry) -> Unit
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
    verticalArrangement = Arrangement.spacedBy(8.dp),
    contentPadding = PaddingValues(vertical = 8.dp),
    reverseLayout = true
  ) {
    val history = historyPage.messages
    items(
      count = history.size,
      key = { index ->
        val item = history[history.lastIndex - index]
        // Fallback key generation for items with ID 0 (pending/failed persistence) to prevent duplicate key crashes
        if (item.id != 0L) item.id else Long.MIN_VALUE + index
      }
    ) { index ->
      val item = history[history.lastIndex - index]
      when (item.kind) {
        HistoryEntryKind.TextMessage -> MessageBubble(
          entry = item,
          onShowMore = chatVM::loadFullMessage,
          zoneId = zoneId,
          formatters = formatters
        )
        HistoryEntryKind.VoiceCall, HistoryEntryKind.VideoCall -> CallLogBubble(
          entry = item,
          zoneId = zoneId,
          formatters = formatters
        )
        HistoryEntryKind.SystemMessage -> SystemMessageBubble(item, onClick = onSystemMessageClick)
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

@Composable
fun MessageBubble(
  entry: HistoryEntry,
  onShowMore: (Long) -> Unit,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>
) {
  val isFromMe = entry.direction == MessageDirection.Sent
  val alignment = if (isFromMe) Alignment.CenterEnd else Alignment.CenterStart
  val colors = if (isFromMe) {
    MaterialTheme.colorScheme.primaryContainer to MaterialTheme.colorScheme.onPrimaryContainer
  } else {
    MaterialTheme.colorScheme.secondaryContainer to MaterialTheme.colorScheme.onSecondaryContainer
  }

  Box(
    modifier = Modifier
      .fillMaxWidth()
      .padding(
        start = if (isFromMe) 48.dp else 8.dp,
        end = if (isFromMe) 8.dp else 48.dp
      ),
    contentAlignment = alignment
  ) {
    Column(
      modifier = Modifier
        .clip(
          RoundedCornerShape(
            topStart = 16.dp,
            topEnd = 16.dp,
            bottomStart = if (isFromMe) 16.dp else 0.dp,
            bottomEnd = if (isFromMe) 0.dp else 16.dp
          )
        )
        .background(colors.first)
        .padding(horizontal = 12.dp, vertical = 8.dp),
      horizontalAlignment = if (isFromMe) Alignment.End else Alignment.Start
    ) {
      Text(
        text = entry.content ?: "",
        color = colors.second,
        style = MaterialTheme.typography.bodyLarge
      )
      if (entry.isTruncated) {
        TextButton(
          onClick = { onShowMore(entry.id) },
          contentPadding = PaddingValues(top = 8.dp, bottom = 0.dp)
        ) {
          Text("Show more")
        }
      }
      Row(
        modifier = Modifier.padding(top = 4.dp, end = 4.dp),
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
              MessageStatus.Delivered -> Icons.Filled.DoneAll
              MessageStatus.Sent -> Icons.Filled.Done
              else -> Icons.Filled.Schedule
            },
            contentDescription = "Message status: ${entry.status}",
            modifier = Modifier.size(16.dp).padding(start = 4.dp),
            tint = if (entry.status == MessageStatus.Read) {
              MaterialTheme.colorScheme.primary
            } else {
              MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
            }
          )
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
        if (entry.kind == HistoryEntryKind.VideoCall) "Outgoing video call" else "Outgoing call"
      )
      else -> Triple(
        Icons.AutoMirrored.Filled.CallMade,
        Color.Red,
        if (entry.kind == HistoryEntryKind.VideoCall) "Unanswered video call" else "Unanswered call"
      )
    }
    CallDirection.Incoming -> when (entry.callStatus) {
      CallStatus.Answered -> Triple(
        Icons.AutoMirrored.Filled.CallReceived,
        MaterialTheme.colorScheme.primary,
        if (entry.kind == HistoryEntryKind.VideoCall) "Incoming video call" else "Incoming call"
      )
      CallStatus.Missed -> Triple(Icons.AutoMirrored.Filled.CallMissed, Color.Red, "Missed call")
      else -> Triple(Icons.AutoMirrored.Filled.CallReceived, Color.Red, "Rejected call")
    }
    null -> Triple(Icons.Default.Call, MaterialTheme.colorScheme.onSurfaceVariant, "Call")
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

  val (displayText, isClickable) = when (parsedAction?.action) {
    "profile_update" -> "You updated your profile." to false
    "profile_update_diff" -> "Profile was updated." to true
    "contact_request_received" -> "You received a contact request." to false
    "contact_request" -> "You sent a contact request." to false
    "contact_confirm" -> {
      if (entry.direction == MessageDirection.Sent) "You accepted the contact request." to false
      else "Your contact request was accepted." to false
    }
    else -> "" to false
  }

  if (displayText.isBlank()) return

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
        text = displayText,
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

@Composable
fun MessageInput(onSendMessage: (String) -> Unit) {
  var text by remember { mutableStateOf(TextFieldValue("")) }
  Box {
    Row(
      modifier = Modifier
        .fillMaxWidth()
        .padding(horizontal = 8.dp, vertical = 4.dp),
      verticalAlignment = Alignment.CenterVertically
    ) {
      TextField(
        value = text,
        onValueChange = { text = it },
        modifier = Modifier
          .weight(1f),
        placeholder = { Text("Message...") },
        colors = TextFieldDefaults.colors(
          focusedIndicatorColor = Color.Transparent,
          unfocusedIndicatorColor = Color.Transparent,
          disabledIndicatorColor = Color.Transparent
        ),
        keyboardOptions = KeyboardOptions(
          capitalization = KeyboardCapitalization.Sentences,
          keyboardType = KeyboardType.Text,
          imeAction = ImeAction.Send
        ),
        keyboardActions = KeyboardActions(
          onSend = {
            if (text.text.isNotBlank()) {
              onSendMessage(text.text)
              text = TextFieldValue("")
            }
          }
        )
      )
      Spacer(modifier = Modifier.width(8.dp))
      IconButton(
        modifier = Modifier
          .background(MaterialTheme.extendedColors.success, shape = CircleShape),
        onClick = {
          if (text.text.isNotBlank()) {
            onSendMessage(text.text)
            text = TextFieldValue("")
          }
        },
        enabled = text.text.isNotBlank()
      ) {
        Icon(Icons.AutoMirrored.Filled.Send,
          contentDescription = "Send Message",
          tint = MaterialTheme.colorScheme.onPrimary
        )
      }
    }
  }
}
