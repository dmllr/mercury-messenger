package hela.eillance.screens

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CallMade
import androidx.compose.material.icons.filled.CallMissed
import androidx.compose.material.icons.filled.CallReceived
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextOverflow
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallRecord
import hela.eillance.ndkio.CallStatus
import java.text.SimpleDateFormat
import java.util.*
import java.util.concurrent.TimeUnit

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CallsScreen(calls: List<CallRecord>) {
    Scaffold(
        topBar = { TopAppBar(title = { Text("Call History") }) }
    ) { padding ->
        if (calls.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize().padding(padding),
                contentAlignment = Alignment.Center
            ) {
                Text("No calls in history.", style = MaterialTheme.typography.bodyLarge)
            }
        } else {
            LazyColumn(modifier = Modifier.padding(padding)) {
                items(calls) { call ->
                    CallHistoryItem(call = call)
                    Divider()
                }
            }
        }
    }
}

@Composable
fun CallHistoryItem(call: CallRecord) {
    val (icon, color) = when (call.direction) {
        CallDirection.Outgoing -> when (call.status) {
            CallStatus.Answered -> Icons.Default.CallMade to MaterialTheme.colorScheme.primary
            else -> Icons.Default.CallMade to Color.Red
        }
        CallDirection.Incoming -> when (call.status) {
            CallStatus.Answered -> Icons.Default.CallReceived to MaterialTheme.colorScheme.primary
            CallStatus.Missed -> Icons.Default.CallMissed to Color.Red
            else -> Icons.Default.CallReceived to Color.Red
        }
    }

    val formattedTime = SimpleDateFormat("MMM d, h:mm a", Locale.getDefault())
        .format(Date(call.timestamp / 1000))

    val durationText = if (call.status == CallStatus.Answered && call.duration_seconds > 0) {
        val minutes = TimeUnit.SECONDS.toMinutes(call.duration_seconds.toLong())
        val seconds = call.duration_seconds - TimeUnit.MINUTES.toSeconds(minutes)
        "($minutes min, $seconds sec)"
    } else {
        "(${call.status.name})"
    }

    ListItem(
        headlineContent = {
            Text(
                text = call.peer_id,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis
            )
        },
        supportingContent = { Text("$formattedTime $durationText") },
        leadingContent = {
            Icon(imageVector = icon, contentDescription = "Call type", tint = color)
        }
    )
}
