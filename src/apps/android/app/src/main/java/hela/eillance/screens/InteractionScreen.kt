package hela.eillance.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.os.ConfigurationCompat
import hela.eillance.MainViewModel
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.concurrent.TimeUnit

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InteractionScreen(
    viewModel: MainViewModel,
    uiState: AppState,
    peerId: String,
    onBack: () -> Unit,
    onNavigateToCall: (String) -> Unit
) {
    // This effect ensures the ViewModel's state is aligned with the screen we are on.
    // It's crucial for when we land here from an incoming call notification.
    LaunchedEffect(peerId) {
        viewModel.selectContact(peerId)
    }

    val listState = rememberLazyListState()
    val isInCall = uiState.callState.isInCall && uiState.callState.peerId == peerId // Use the peerId from the route

    // This effect handles navigation to the call screen when a call starts.
    LaunchedEffect(isInCall) {
        if (isInCall) {
            onNavigateToCall(peerId)
        }
    }

    val historyItems = uiState.interactionHistory

    LaunchedEffect(historyItems.size) {
        if (historyItems.isNotEmpty()) {
            listState.animateScrollToItem(historyItems.size - 1)
        }
    }

    LaunchedEffect(uiState.interactionHistory) {
        val unreadMessageIds = uiState.interactionHistory
            .filter { it.kind == HistoryEntryKind.TextMessage && it.direction == MessageDirection.Received && it.status != MessageStatus.Read }
            .map { it.id }
        if (unreadMessageIds.isNotEmpty()) {
            viewModel.MarkMessagesAsRead(unreadMessageIds)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(peerId, maxLines = 1, overflow = TextOverflow.Ellipsis) },
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
                    IconButton(onClick = { /* TODO: Navigate to contact profile */ }) {
                        Icon(Icons.Default.Person, contentDescription = "Contact Profile")
                    }
                }
            )
        },
        bottomBar = {
            MessageInput(onSendMessage = viewModel::sendMessage)
        }
    ) { padding ->
        ChatHistory(
            modifier = Modifier.padding(padding),
            uiState = uiState,
            listState = listState,
            viewModel = viewModel
        )
    }
}

@Composable
fun ChatHistory(
    modifier: Modifier = Modifier,
    uiState: AppState,
    listState: androidx.compose.foundation.lazy.LazyListState,
    viewModel: MainViewModel
) {
    LazyColumn(
        state = listState,
        modifier = modifier.fillMaxSize(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
        contentPadding = PaddingValues(vertical = 8.dp)
    ) {
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
        items(uiState.interactionHistory, key = { it.id }) { item ->
            when (item.kind) {
                HistoryEntryKind.TextMessage -> MessageBubble(item)
                HistoryEntryKind.VoiceCall, HistoryEntryKind.VideoCall -> CallLogBubble(item)
                HistoryEntryKind.SystemMessage -> SystemMessageBubble(item)
            }
        }
    }
}

@Composable
fun MessageBubble(entry: HistoryEntry) {
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
        Column(horizontalAlignment = if (isFromMe) Alignment.End else Alignment.Start) {
            Text(
                text = entry.content ?: "",
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
                color = colors.second,
                style = MaterialTheme.typography.bodyLarge
            )
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
                            else -> Icons.Filled.Schedule // Sending or None
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
        null -> Triple(Icons.Default.Call, MaterialTheme.colorScheme.onSurfaceVariant, "Call") // Fallback
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
fun SystemMessageBubble(entry: HistoryEntry) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.Center
    ) {
        Text(
            text = entry.content ?: "",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
fun formatTimestamp(timestamp: Long): String {
    val context = LocalContext.current
    val locale = ConfigurationCompat.getLocales(context.resources.configuration)[0]
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
