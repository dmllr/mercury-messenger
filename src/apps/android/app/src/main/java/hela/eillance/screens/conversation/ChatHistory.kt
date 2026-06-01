package hela.eillance.screens.conversation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import hela.eillance.ndkio.Reaction
import hela.eillance.viewmodels.ConversationViewModel
import kotlinx.coroutines.launch
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale
import kotlin.math.abs
import kotlin.math.roundToInt

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
  activeMessageActionId: Long?,
  editingMessageId: Long?,
  myId: String,
  contactName: String,
  onSwipeToReply: (HistoryEntry) -> Unit,
  onReplyClick: (Long) -> Unit,
  onToggleSelection: (Long) -> Unit,
  onMessageClick: (HistoryEntry) -> Unit,
  onDismissMenu: () -> Unit,
  onActionReact: (Long, Reaction) -> Unit,
  onActionCopy: (String) -> Unit,
  onActionEdit: (Long) -> Unit,
  onActionDelete: (Long) -> Unit,
  onActionShare: (HistoryEntry) -> Unit,
  onActionForward: (HistoryEntry) -> Unit,
  onEditCommit: (Long, String) -> Unit,
  onEditCancel: () -> Unit,
  onSystemMessageClick: (HistoryEntry) -> Unit,
  onMediaClick: (List<Attachment>, Int) -> Unit,
  resolveContactName: (String) -> String
) {
  val formatters = remember(locale) {
    Triple(
      DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT).withLocale(locale),
      DateTimeFormatter.ofPattern("MMM d, HH:mm", locale),
      DateTimeFormatter.ofLocalizedDateTime(FormatStyle.SHORT, FormatStyle.SHORT).withLocale(locale)
    )
  }

  Box(modifier = modifier) {
    LazyColumn(
      state = listState,
      modifier = Modifier
        .fillMaxSize()
        .padding(start = 12.dp, end = 12.dp),
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
        val olderItem = if (index < history.size - 1) history[history.lastIndex - (index + 1)] else null

        val isGroupTail = !shouldGroupMessages(currentItem, newerItem)
        val isGroupHead = !shouldGroupMessages(currentItem, olderItem)

        val isDateBoundary = olderItem == null || !isSameDay(currentItem.timestamp, olderItem.timestamp, zoneId)

        // Mark as read when rendered
        if (currentItem.direction == MessageDirection.Received && currentItem.status != MessageStatus.Read) {
          LaunchedEffect(currentItem.id) {
            chatVM.onMessageShown(currentItem.id)
          }
        }

        val repliedMessage = remember<HistoryEntry?>(currentItem.replyToId) {
          currentItem.replyToId?.let { id ->
            history.find { it.id == id } ?: chatVM.getHistoryEntry(id)
          }
        }

        Column(modifier = Modifier.fillMaxWidth()) {
          if (isDateBoundary) {
            DateSeparator(timestamp = currentItem.timestamp, zoneId = zoneId, locale = locale)
          }

          when (currentItem.kind) {
            HistoryEntryKind.TextMessage -> MessageBubble(
              entry = currentItem,
              onShowMore = chatVM::loadFullMessage,
              transferProgress = transferProgress,
              zoneId = zoneId,
              formatters = formatters,
              onMediaClick = onMediaClick,
              repliedMessage = repliedMessage,
              onSwipeToReply = { onSwipeToReply(currentItem) },
              onSwipeToForward = { onActionForward(currentItem) },
              onReplyClick = onReplyClick,
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
              onToggleSelection = { onToggleSelection(currentItem.id) },
              onMessageClick = { onMessageClick(currentItem) },
              isMenuVisible = activeMessageActionId == currentItem.id,
              onDismissMenu = onDismissMenu,
              myId = myId,
              contactName = contactName,
              onActionReact = { r -> onActionReact(currentItem.id, r) },
              onActionCopy = { content -> onActionCopy(content) },
              onActionEdit = { onActionEdit(currentItem.id) },
              onActionDelete = { onActionDelete(currentItem.id) },
              onActionShare = { onActionShare(currentItem) },
              onActionForward = { onActionForward(currentItem) },
              isEditing = editingMessageId == currentItem.id,
              onEditCommit = { newText -> onEditCommit(currentItem.id, newText) },
              onEditCancel = onEditCancel,
              resolveContactName = resolveContactName
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

    if (historyPage.messages.isNotEmpty()) {
      FastScroller(
        listState = listState,
        history = historyPage.messages,
        zoneId = zoneId,
        locale = locale
      )
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
  if (current.status != other.status) return false

  val diff = abs(current.timestamp - other.timestamp)
  return diff <= GROUPING_THRESHOLD_US
}

@Composable
private fun DateSeparator(timestamp: Long, zoneId: ZoneId, locale: Locale) {
  val text = formatDateSeparatorText(timestamp, zoneId, locale)
  Box(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 12.dp),
    contentAlignment = Alignment.Center
  ) {
    Box(
      modifier = Modifier
        .background(
          color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.7f),
          shape = RoundedCornerShape(12.dp)
        )
        .padding(horizontal = 12.dp, vertical = 4.dp)
    ) {
      Text(
        text = text,
        style = MaterialTheme.typography.labelMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        fontWeight = FontWeight.Medium
      )
    }
  }
}

@Composable
private fun FastScroller(
  listState: LazyListState,
  history: List<HistoryEntry>,
  zoneId: ZoneId,
  locale: Locale
) {
  val coroutineScope = rememberCoroutineScope()
  var isDragging by remember { mutableStateOf(false) }
  var dragProgress by remember { mutableFloatStateOf(0f) }
  var thumbHeight by remember { mutableFloatStateOf(0f) }

  val isVisible = isDragging || listState.isScrollInProgress
  val firstVisibleItemIndex by remember { derivedStateOf { listState.firstVisibleItemIndex } }

  val displayProgress = if (isDragging) {
    dragProgress
  } else {
    if (history.size <= 1) 1f
    else 1f - (firstVisibleItemIndex / history.lastIndex.toFloat()).coerceIn(0f, 1f)
  }

  val activeIndex = ((1f - displayProgress) * history.lastIndex).roundToInt().coerceIn(0, history.lastIndex)
  val activeMessage = history.getOrNull(history.lastIndex - activeIndex)

  BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
    val maxHeight = constraints.maxHeight.toFloat()
    val safeMaxHeight = if (maxHeight > 0f) maxHeight else 1f

    Box(modifier = Modifier.fillMaxSize()) {
      // Visual track indicator for the scroll area
      Box(
        modifier = Modifier
          .align(Alignment.CenterEnd)
          .padding(end = 2.dp)
          .width(4.dp)
          .fillMaxHeight(0.95f)
          .background(
            color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.15f),
            shape = RoundedCornerShape(2.dp)
          )
      )

      AnimatedVisibility(
        visible = isVisible,
        enter = fadeIn(),
        exit = fadeOut(),
        modifier = Modifier.align(Alignment.TopEnd)
      ) {
        Row(
          verticalAlignment = Alignment.CenterVertically,
          modifier = Modifier
            .offset { IntOffset(0, ((safeMaxHeight - thumbHeight) * displayProgress).roundToInt()) }
            .padding(end = 2.dp)
        ) {
          if (activeMessage != null) {
            val dateText = formatDateSeparatorText(activeMessage.timestamp, zoneId, locale)
            Box(
              modifier = Modifier
                .padding(end = 8.dp)
                .background(
                  color = MaterialTheme.colorScheme.secondaryContainer,
                  shape = RoundedCornerShape(16.dp)
                )
                .padding(horizontal = 12.dp, vertical = 6.dp)
            ) {
              Text(
                text = dateText,
                style = MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSecondaryContainer,
                fontWeight = FontWeight.Bold
              )
            }
          }

          Box(
            modifier = Modifier
              .width(4.dp)
              .height(48.dp)
              .onSizeChanged { thumbHeight = it.height.toFloat() }
              .clip(RoundedCornerShape(2.dp))
              .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.8f))
          )
        }
      }

      Box(
        modifier = Modifier
          .align(Alignment.TopEnd)
          .width(16.dp)
          .fillMaxHeight()
          .pointerInput(history.size) {
            detectVerticalDragGestures(
              onDragStart = {
                dragProgress = displayProgress
                isDragging = true
              },
              onDragEnd = { isDragging = false },
              onDragCancel = { isDragging = false },
              onVerticalDrag = { change, dragAmount ->
                change.consume()
                val trackHeight = safeMaxHeight - thumbHeight
                if (trackHeight > 0f) {
                  dragProgress = (dragProgress + (dragAmount / trackHeight)).coerceIn(0f, 1f)
                  val targetIndex = ((1f - dragProgress) * history.lastIndex).roundToInt().coerceIn(0, history.lastIndex)
                  coroutineScope.launch { listState.scrollToItem(targetIndex) }
                }
              }
            )
          }
      )
    }
  }
}
