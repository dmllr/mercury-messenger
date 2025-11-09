package hela.eillance.screens
import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.ExperimentalFoundationApi
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.os.ConfigurationCompat
import hela.eillance.MainViewModel
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import hela.eillance.ndkio.PeerConnectionState
import hela.eillance.theme.extendedColors
import hela.eillance.utils.rememberImageBitmapFromBase64
import kotlinx.coroutines.launch
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.concurrent.TimeUnit

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConversationScreen(
  viewModel: MainViewModel,
  uiState: AppState,
  peerId: String,
  onBack: () -> Unit,
  onNavigateToCall: (String) -> Unit,
  onShowContactProfile: (String) -> Unit
) {
  LaunchedEffect(peerId) {
    viewModel.selectContact(peerId)
  }

  val listState = rememberLazyListState()
  val coroutineScope = rememberCoroutineScope()
  val history = uiState.interactionHistory
  var previousHistory by remember(peerId) { mutableStateOf(history) }
  val contact = uiState.contacts.find { it.peerId == peerId }

  val isInCall = uiState.callState.isInCall && uiState.callState.peerId == peerId

  var systemMessageToShowDetails by remember { mutableStateOf<HistoryEntry?>(null) }
  var tappedTruncatedMessageId by remember { mutableStateOf<Long?>(null) }

  LaunchedEffect(isInCall) {
    if (isInCall) {
      onNavigateToCall(peerId)
    }
  }

  // This effect handles auto-scrolling when new messages are added.
  LaunchedEffect(history) {
    // We only scroll if the list has actually grown, not just on recomposition.
    if (history.size > previousHistory.size) {
      coroutineScope.launch {
        listState.animateScrollToItem(0)
      }
    }
    previousHistory = history
  }


  LaunchedEffect(uiState.interactionHistory) {
    val unreadMessageIds = uiState.interactionHistory
      .filter { it.kind == HistoryEntryKind.TextMessage && it.direction == MessageDirection.Received && it.status != MessageStatus.Read }
      .map { it.id }
    if (unreadMessageIds.isNotEmpty()) {
      viewModel.MarkMessagesAsRead(unreadMessageIds)
    }
  }

  // This effect watches for when a tapped truncated message has been loaded.
  // When it's loaded (no longer truncated), it triggers the details dialog.
  val loadedMessage = if (tappedTruncatedMessageId != null) {
    uiState.interactionHistory.find { it.id == tappedTruncatedMessageId && !it.isTruncated }
  } else {
    null
  }

  LaunchedEffect(loadedMessage) {
    if (loadedMessage != null) {
      systemMessageToShowDetails = loadedMessage
      tappedTruncatedMessageId = null // Reset the trigger
    }
  }

  // Display the dialog when its state is set
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
                        androidx.compose.foundation.Image(
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
          IconButton(onClick = { viewModel.startVoiceCall() }) {
            Icon(Icons.Default.Call, contentDescription = "Voice Call")
          }
          IconButton(onClick = { viewModel.startVideoCall() }) {
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
      contact?.let {
        ConnectionStatusBar(contact = it)
      }

      ChatHistory(
        modifier = Modifier.weight(1f),
        uiState = uiState,
        listState = listState,
        viewModel = viewModel,
        onSystemMessageClick = { clickedEntry ->
          // If the message content is truncated, first trigger a load.
          // The LaunchedEffect above will handle showing the dialog once loaded.
          if (clickedEntry.isTruncated) {
            tappedTruncatedMessageId = clickedEntry.id
            viewModel.loadFullMessage(clickedEntry.id)
          } else {
            systemMessageToShowDetails = clickedEntry
          }
        }
      )
      MessageInput(onSendMessage = viewModel::sendMessage)
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
          Box(modifier = Modifier
            .size(10.dp)
            .background(statusColor, RoundedCornerShape(2.dp)))
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
  uiState: AppState,
  listState: androidx.compose.foundation.lazy.LazyListState,
  viewModel: MainViewModel,
  onSystemMessageClick: (HistoryEntry) -> Unit
) {
  LazyColumn(
    state = listState,
    modifier = modifier.fillMaxSize(),
    verticalArrangement = Arrangement.spacedBy(8.dp),
    contentPadding = PaddingValues(vertical = 8.dp),
    reverseLayout = true
  ) {
    // The `items` block is defined first, so its contents will be rendered from the bottom up.
    // The "Load Older" button is defined last, so it appears at the top of the scrollable area.

    val history = uiState.interactionHistory
    items(
      count = history.size,
      key = { index -> history[history.lastIndex - index].id }
    ) { index ->
      val item = history[history.lastIndex - index]
      when (item.kind) {
        HistoryEntryKind.TextMessage -> MessageBubble(item, onShowMore = viewModel::loadFullMessage)
        HistoryEntryKind.VoiceCall, HistoryEntryKind.VideoCall -> CallLogBubble(item)
        HistoryEntryKind.SystemMessage -> SystemMessageBubble(item, onClick = onSystemMessageClick)
      }
    }

    if (uiState.hasOlderMessages) {
      item {
        Button(
          onClick = viewModel::loadOlderMessages,
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
fun MessageBubble(entry: HistoryEntry, onShowMore: (Long) -> Unit) {
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
          text = formatTimestamp(entry.timestamp),
          style = MaterialTheme.typography.bodySmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.8f)
        )
        if (isFromMe) {
          Spacer(Modifier.width(4.dp))
          Icon(
            imageVector = when(entry.status) {
              MessageStatus.Read -> Icons.Filled.DoneAll
              MessageStatus.Delivered -> Icons.Filled.DoneAll
              MessageStatus.Sent -> Icons.Filled.Done
              else -> Icons.Filled.Schedule
            },
            contentDescription = "Message status: ${entry.status}",
            modifier = Modifier.padding(start = 4.dp),
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
fun CallLogBubble(entry: HistoryEntry) {
  val (icon, color, text) = when (entry.callDirection) {
    CallDirection.Outgoing -> when (entry.callStatus) {
      CallStatus.Answered -> Triple(Icons.AutoMirrored.Filled.CallMade, MaterialTheme.colorScheme.primary, if(entry.kind == HistoryEntryKind.VideoCall) "Outgoing video call" else "Outgoing call")
      else -> Triple(Icons.AutoMirrored.Filled.CallMade, Color.Red, if(entry.kind == HistoryEntryKind.VideoCall) "Unanswered video call" else "Unanswered call")
    }
    CallDirection.Incoming -> when (entry.callStatus) {
      CallStatus.Answered -> Triple(Icons.AutoMirrored.Filled.CallReceived, MaterialTheme.colorScheme.primary, if(entry.kind == HistoryEntryKind.VideoCall) "Incoming video call" else "Incoming call")
      CallStatus.Missed -> Triple(Icons.AutoMirrored.Filled.CallMissed, Color.Red, "Missed call")
      else -> Triple(Icons.AutoMirrored.Filled.CallReceived, Color.Red, "Rejected call")
    }
    null -> Triple(Icons.Default.Call, MaterialTheme.colorScheme.onSurfaceVariant, "Call")
  }

  val formattedTime = formatTimestamp(entry.timestamp)

  val duration = entry.durationSeconds ?: 0
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
    Icon(imageVector = icon, contentDescription = text, tint = color, modifier = Modifier.padding(end = 8.dp))
    Text(
      text = "$text - $formattedTime$durationText",
      style = MaterialTheme.typography.bodySmall,
      color = MaterialTheme.colorScheme.onSurfaceVariant
    )
  }
}

@Composable
fun SystemMessageBubble(entry: HistoryEntry, onClick: (HistoryEntry) -> Unit) {
  // This JSON parser is lenient and will ignore fields it doesn't recognize.
  val json = Json { ignoreUnknownKeys = true }

  // Attempt to parse the content as a structured system message.
  val parsedAction = remember(entry.content, entry.isTruncated) {
    entry.content?.let { content ->
      try {
        // For truncated messages, we can't fully parse, but we can reliably get the action
        // because it's at the beginning of the JSON. If full parsing fails, we fall back to regex.
        json.decodeFromString<JsonAction>(content)
      } catch (e: Exception) {
        // This will catch errors for both truncated and malformed JSON
        val actionMatch = Regex("\"action\"\\s*:\\s*\"([^\"]+)\"").find(content)
        actionMatch?.groupValues?.get(1)?.let { action -> JsonAction(action) }
      }
    }
  }

  val (displayText, isClickable) = when (parsedAction?.action) {
    "profile_update" -> {
      "You updated your profile." to false
    }
    "profile_update_diff" -> {
      "Profile was updated." to true
    }
    "contact_request_received" -> "You received a contact request." to false
    "contact_request" -> "You sent a contact request." to false
    "contact_confirm" -> {
      if (entry.direction == MessageDirection.Sent) {
        "You accepted the contact request." to false
      } else {
        "Your contact request was accepted." to false
      }
    }
    // If parsing fails completely, show nothing. It's better than showing broken JSON.
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

/**
 * A simple data class for parsing only the 'action' field from a JSON string
 * to determine how to render a system message.
 */
@Serializable
private data class JsonAction(val action: String)

@Composable
fun formatTimestamp(timestamp: Long): String {
  val locale = ConfigurationCompat.getLocales(LocalConfiguration.current)[0]
    ?: java.util.Locale.getDefault()

  val messageTime = Instant.ofEpochMilli(timestamp / 1000).atZone(ZoneId.systemDefault())
  val now = ZonedDateTime.now(ZoneId.systemDefault())

  val isToday = messageTime.toLocalDate().isEqual(now.toLocalDate())
  val isYesterday = messageTime.toLocalDate().isEqual(now.toLocalDate().minusDays(1))

  return when {
    isToday -> messageTime.format(DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT).withLocale(locale))
    isYesterday -> {
      "Yesterday, " + messageTime.format(DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT).withLocale(locale))
    }
    messageTime.year == now.year -> {
      messageTime.format(DateTimeFormatter.ofPattern("MMM d, HH:mm", locale))
    }
    else -> {
      messageTime.format(DateTimeFormatter.ofLocalizedDateTime(FormatStyle.SHORT, FormatStyle.SHORT).withLocale(locale))
    }
  }
}

@Composable
fun MessageInput(onSendMessage: (String) -> Unit) {
  var text by remember { mutableStateOf(TextFieldValue("")) }
  Surface(tonalElevation = 3.dp) {
    Row(
      modifier = Modifier
        .fillMaxWidth()
        .padding(horizontal = 8.dp, vertical = 4.dp),
      verticalAlignment = Alignment.CenterVertically
    ) {
      TextField(
        value = text,
        onValueChange = { text = it },
        modifier = Modifier.weight(1f),
        placeholder = { Text("Message...") },
        shape = RoundedCornerShape(24.dp),
        colors = TextFieldDefaults.colors(
          focusedIndicatorColor = Color.Transparent,
          unfocusedIndicatorColor = Color.Transparent,
          disabledIndicatorColor = Color.Transparent
        )
      )
      Spacer(modifier = Modifier.width(8.dp))
      IconButton(
        onClick = {
          if (text.text.isNotBlank()) {
            onSendMessage(text.text)
            text = TextFieldValue("")
          }
        },
        enabled = text.text.isNotBlank()
      ) {
        Icon(Icons.AutoMirrored.Filled.Send, contentDescription = "Send Message")
      }
    }
  }
}
