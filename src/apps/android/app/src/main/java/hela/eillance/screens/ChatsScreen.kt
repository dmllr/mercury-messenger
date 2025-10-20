package hela.eillance.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import hela.eillance.ndkio.ChatSummary
import hela.eillance.ndkio.MessageDirection
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ChatsScreen(
    chats: List<ChatSummary>,
    onChatClick: (String) -> Unit
) {
    Scaffold(
        topBar = { TopAppBar(title = { Text("Chats") }) }
    ) { padding ->
        if (chats.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize().padding(padding),
                contentAlignment = Alignment.Center
            ) {
                Text("No chats yet.", style = MaterialTheme.typography.bodyLarge)
            }
        } else {
            LazyColumn(modifier = Modifier.padding(padding)) {
                items(chats, key = { it.peer_id }) { chat ->
                    ChatItem(chat = chat, onClick = { onChatClick(chat.peer_id) })
                    Divider()
                }
            }
        }
    }
}

@Composable
fun ChatItem(chat: ChatSummary, onClick: () -> Unit) {
    val formattedTime = SimpleDateFormat("h:mm a", Locale.getDefault())
        .format(Date(chat.last_message.timestamp / 1000)) // Assuming timestamp is in microseconds

    ListItem(
        modifier = Modifier.clickable(onClick = onClick),
        headlineContent = {
            Text(
                text = chat.peer_id,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                fontWeight = FontWeight.Bold
            )
        },
        supportingContent = {
            val prefix = if (chat.last_message.direction == MessageDirection.Sent) "You: " else ""
            Text(
                text = "$prefix${chat.last_message.content}",
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        },
        trailingContent = {
            Text(text = formattedTime, style = MaterialTheme.typography.bodySmall)
        }
    )
}
