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
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Send
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
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.MainViewModel
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.MessageDirection
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.TimeUnit

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun InteractionScreen(
    viewModel: MainViewModel,
    uiState: AppState,
    onBack: () -> Unit,
) {
    val peerId = uiState.selectedContactId ?: "Unknown"
    val listState = rememberLazyListState()
    val historyItems = uiState.interactionHistory

    LaunchedEffect(historyItems.size) {
        if (historyItems.isNotEmpty()) {
            listState.animateScrollToItem(historyItems.size - 1)
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
        LazyColumn(
            state = listState,
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
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
            items(historyItems, key = { it.timestamp }) { item ->
                when (item.kind) {
                    HistoryEntryKind.TextMessage -> MessageBubble(item)
                    HistoryEntryKind.VoiceCall, HistoryEntryKind.VideoCall -> CallLogBubble(item)
                    HistoryEntryKind.SystemMessage -> SystemMessageBubble(item)
                }
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
                start = if (isFromMe) 64.dp else 8.dp,
                end = if (isFromMe) 8.dp else 64.dp
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

    val formattedTime = SimpleDateFormat("MMM d, h:mm a", Locale.getDefault())
        .format(Date(entry.timestamp / 1000))

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
                Icon(Icons.Default.Send, contentDescription = "Send Message")
            }
        }
    }
}
