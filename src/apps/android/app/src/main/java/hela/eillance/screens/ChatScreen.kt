package hela.eillance.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Send
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.AppState
import hela.eillance.ndkio.HistoryMessage
import hela.eillance.ndkio.MessageDirection

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChatScreen(
    uiState: AppState,
    onSendMessage: (String) -> Unit,
    onLoadOlder: () -> Unit,
    onBack: () -> Unit,
) {
    val peerId = uiState.selectedContactId ?: "Unknown"
    val listState = rememberLazyListState()

    // Scroll to bottom when a new message arrives
    LaunchedEffect(uiState.chatHistory.size) {
        if (uiState.chatHistory.isNotEmpty()) {
            listState.animateScrollToItem(uiState.chatHistory.size - 1)
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(peerId, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.Default.ArrowBack, contentDescription = "Back")
                    }
                }
            )
        },
        bottomBar = {
            MessageInput(onSendMessage = onSendMessage)
        }
    ) { padding ->
        LazyColumn(
            state = listState,
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .padding(horizontal = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
            contentPadding = PaddingValues(vertical = 8.dp)
        ) {
            if (uiState.hasOlderMessages) {
                item {
                    Button(
                        onClick = onLoadOlder,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("Load Older Messages")
                    }
                }
            }
            items(uiState.chatHistory) { message ->
                MessageBubble(message = message)
            }
        }
    }
}

@Composable
fun MessageBubble(message: HistoryMessage) {
    val isFromMe = message.direction == MessageDirection.Sent
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
                start = if (isFromMe) 64.dp else 0.dp,
                end = if (isFromMe) 0.dp else 64.dp
            ),
        contentAlignment = alignment
    ) {
        Text(
            text = message.content,
            modifier = Modifier
                .clip(RoundedCornerShape(16.dp))
                .background(colors.first)
                .padding(horizontal = 12.dp, vertical = 8.dp),
            color = colors.second,
            style = MaterialTheme.typography.bodyLarge
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
                .padding(8.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            TextField(
                value = text,
                onValueChange = { text = it },
                modifier = Modifier.weight(1f),
                placeholder = { Text("Type a message...") },
                colors = TextFieldDefaults.colors(
                    focusedIndicatorColor = Color.Transparent,
                    unfocusedIndicatorColor = Color.Transparent,
                    disabledIndicatorColor = Color.Transparent
                )
            )
            Spacer(modifier = Modifier.width(8.dp))
            IconButton(onClick = {
                onSendMessage(text.text)
                text = TextFieldValue("")
            }) {
                Icon(Icons.Default.Send, contentDescription = "Send Message")
            }
        }
    }
}
