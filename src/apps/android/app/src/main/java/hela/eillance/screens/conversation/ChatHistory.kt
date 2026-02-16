package hela.eillance.screens.conversation

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import hela.eillance.viewmodels.ConversationViewModel
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import kotlin.math.abs

@Composable
fun ChatHistory(
  modifier: Modifier = Modifier,
  historyPage: HistoryPage,
  listState: LazyListState,
  chatVM: ConversationViewModel,
  transferProgress: Map<String, Float>,
  locale: Locale,
  zoneId: ZoneId,
  selectedMessageIds: Set<Long>,
  selectionMode: Boolean,
  onToggleSelection: (Long) -> Unit,
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
      val olderItem =
        if (index < history.size - 1) history[history.lastIndex - (index + 1)] else null

      val isGroupTail = !shouldGroupMessages(currentItem, newerItem)
      val isGroupHead = !shouldGroupMessages(currentItem, olderItem)

      // Mark as read when rendered
      if (currentItem.direction == MessageDirection.Received && currentItem.status != MessageStatus.Read) {
        LaunchedEffect(currentItem.id) {
          chatVM.onMessageShown(currentItem.id)
        }
      }

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
          onCancelTransfer = { attId ->
            chatVM.cancelTransfer(
              attId,
              currentItem.direction == MessageDirection.Sent
            )
          },
          onResumeTransfer = { attId ->
            chatVM.resumeTransfer(
              attId,
              currentItem.direction == MessageDirection.Sent
            )
          },
          onDiscardTransfer = { attId -> chatVM.discardAttachment(attId) },
          isSelected = selectedMessageIds.contains(currentItem.id),
          isUnread = currentItem.direction == MessageDirection.Received && currentItem.status != MessageStatus.Read,
          selectionMode = selectionMode,
          onToggleSelection = { onToggleSelection(currentItem.id) }
        )

        HistoryEntryKind.VoiceCall, HistoryEntryKind.VideoCall -> CallLogBubble(
          entry = currentItem,
          zoneId = zoneId,
          formatters = formatters,
          isUnread = currentItem.direction == MessageDirection.Received && currentItem.status != MessageStatus.Read
        )

        HistoryEntryKind.SystemMessage -> SystemMessageBubble(
          entry = currentItem,
          onClick = onSystemMessageClick,
          isUnread = currentItem.direction == MessageDirection.Received && currentItem.status != MessageStatus.Read
        )
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
          Text(stringResource(id = R.string.action_load_older_messages))
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
