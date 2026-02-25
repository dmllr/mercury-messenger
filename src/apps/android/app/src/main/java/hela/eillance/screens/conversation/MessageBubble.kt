package hela.eillance.screens.conversation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Done
import androidx.compose.material.icons.filled.DoneAll
import androidx.compose.material.icons.filled.Schedule
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.components.MediaBubbleContent
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.MessageDirection
import hela.eillance.ndkio.MessageStatus
import hela.eillance.ndkio.Reaction
import hela.eillance.theme.extendedColors
import java.time.ZoneId
import java.time.format.DateTimeFormatter

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun MessageBubble(
  entry: HistoryEntry,
  onShowMore: (Long) -> Unit,
  transferProgress: Map<String, Float>,
  zoneId: ZoneId,
  formatters: Triple<DateTimeFormatter, DateTimeFormatter, DateTimeFormatter>,
  onMediaClick: (List<Attachment>, Int) -> Unit,
  isGroupHead: Boolean = true,
  isGroupTail: Boolean = true,
  onCancelTransfer: (String) -> Unit,
  onResumeTransfer: (String) -> Unit,
  onDiscardTransfer: (String) -> Unit,
  isSelected: Boolean = false,
  isUnread: Boolean = false,
  selectionMode: Boolean = false,
  onToggleSelection: () -> Unit = {},
  onMessageClick: () -> Unit = {},
  isMenuVisible: Boolean = false,
  onDismissMenu: () -> Unit = {},
  myId: String = "",
  onActionReact: (Reaction) -> Unit = {},
  onActionCopy: (String) -> Unit = {},
  onActionEdit: () -> Unit = {},
  onActionDelete: () -> Unit = {},
  isEditing: Boolean = false,
  onEditCommit: (String) -> Unit = {},
  onEditCancel: () -> Unit = {}
) {
  val isFromMe = entry.direction == MessageDirection.Sent
  val alignment = if (isFromMe) Alignment.CenterEnd else Alignment.CenterStart

  if (entry.isDeleted) {
    Box(
      modifier = Modifier
        .fillMaxWidth()
        .padding(vertical = 4.dp, horizontal = 16.dp),
      contentAlignment = alignment
    ) {
      Text(
        text = stringResource(R.string.message_deleted),
        style = MaterialTheme.typography.bodySmall,
        fontStyle = FontStyle.Italic,
        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f)
      )
    }
    return
  }

  val colors = if (isFromMe) {
    MaterialTheme.extendedColors.sentBubbleBackground to MaterialTheme.extendedColors.sentBubbleContent
  } else {
    MaterialTheme.extendedColors.receivedBubbleBackground to MaterialTheme.extendedColors.receivedBubbleContent
  }

  val topStartRadius = if (!isFromMe && !isGroupHead) 4.dp else 16.dp
  val bottomStartRadius = if (!isFromMe && !isGroupTail) 4.dp else 16.dp
  val topEndRadius = if (isFromMe && !isGroupHead) 4.dp else 16.dp
  val bottomEndRadius = if (isFromMe && !isGroupTail) 4.dp else 16.dp

  val bubbleShape = RoundedCornerShape(
    topStart = topStartRadius,
    topEnd = topEndRadius,
    bottomStart = bottomStartRadius,
    bottomEnd = bottomEndRadius
  )

  val topPadding = if (isGroupHead) 8.dp else 0.dp

  Box(
    modifier = Modifier
      .fillMaxWidth()
      .padding(
        start = if (isFromMe) 48.dp else 8.dp,
        end = if (isFromMe) 8.dp else 48.dp,
        top = topPadding
      ),
    contentAlignment = alignment
  ) {
    // Menu Popup Logic
    if (isMenuVisible) {
      val myReactions = entry.reactions
        .filter { it.authorId == myId }
        .mapNotNull { it.reaction }
        .toSet()

      MessageActionMenu(
        onDismiss = onDismissMenu,
        onCopy = { entry.content?.let { onActionCopy(it) } ?: onDismissMenu() },
        onDelete = onActionDelete,
        onEdit = if (isFromMe && !entry.content.isNullOrBlank()) onActionEdit else null,
        onReact = onActionReact,
        currentReactions = myReactions,
        isFromMe = isFromMe
      )
    }

    val bubbleContent = @Composable {
      Column(
        modifier = Modifier
          .then(
            if (isSelected) Modifier.border(2.dp, MaterialTheme.colorScheme.primary, bubbleShape)
            else Modifier
          )
          .clip(bubbleShape)
          .background(colors.first)
          .then(
            if (isSelected) Modifier.background(MaterialTheme.colorScheme.primary.copy(alpha = 0.15f))
            else Modifier
          )
          .combinedClickable(
            onClick = {
              if (selectionMode) onToggleSelection()
              else onMessageClick()
            },
            onLongClick = {
              if (!selectionMode) onToggleSelection()
            }
          )
          .padding(horizontal = 4.dp, vertical = 4.dp),
        horizontalAlignment = if (isFromMe) Alignment.End else Alignment.Start
      ) {
        // Render Attachments
        if (entry.attachments.isNotEmpty()) {
          MediaBubbleContent(
            attachments = entry.attachments,
            transferProgress = transferProgress,
            isFromMe = isFromMe,
            selectionMode = selectionMode,
            onMediaClick = { clickedAttachment ->
              val index = entry.attachments.indexOf(clickedAttachment)
              onMediaClick(entry.attachments, if (index != -1) index else 0)
            },
            onCancel = onCancelTransfer,
            onResume = onResumeTransfer,
            onDiscard = onDiscardTransfer
          )

          if (!entry.content.isNullOrBlank()) {
            Spacer(modifier = Modifier.height(8.dp))
          }
        }

        // Render Text or Editor
        if (isEditing && !entry.content.isNullOrBlank()) {
          MessageEditor(
            initialContent = entry.content,
            onCommit = onEditCommit,
            onCancel = onEditCancel,
            modifier = Modifier.padding(horizontal = 4.dp, vertical = 4.dp)
          )
        } else if (!entry.content.isNullOrBlank()) {
          val styledContent = remember(entry.content) {
            buildMessageContent(entry.content!!)
          }

          Text(
            text = styledContent,
            color = colors.second,
            style = MaterialTheme.typography.bodyLarge,
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp)
          )
          if (entry.isTruncated) {
            TextButton(
              onClick = { if (!selectionMode) onShowMore(entry.id) else onToggleSelection() },
              contentPadding = PaddingValues(top = 8.dp, bottom = 0.dp)
            ) {
              Text(stringResource(id = R.string.action_show_more))
            }
          }
        }

        // Edit Label & Reactions (Hide if editing)
        if (!isEditing && (entry.isEdited || entry.reactions.isNotEmpty())) {
          Row(
            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
            verticalAlignment = Alignment.CenterVertically
          ) {
            if (entry.isEdited) {
              Text(
                text = stringResource(R.string.message_edited),
                style = MaterialTheme.typography.labelSmall,
                color = colors.second.copy(alpha = 0.6f),
                modifier = Modifier.padding(end = 6.dp)
              )
            }
            if (entry.reactions.isNotEmpty()) {
              // Group reactions by type for display
              val reactionStr = entry.reactions
                .mapNotNull { it.reaction?.symbol }
                .distinct()
                .joinToString(" ")

              if (reactionStr.isNotEmpty()) {
                Text(
                  text = reactionStr,
                  style = MaterialTheme.typography.labelSmall,
                  color = colors.second
                )
                if (entry.reactions.size > 1) {
                  Text(
                    text = " ${entry.reactions.size}",
                    style = MaterialTheme.typography.labelSmall,
                    color = colors.second.copy(alpha = 0.7f)
                  )
                }
              }
            }
          }
        }

        if (isGroupTail && !isEditing) {
          Row(
            modifier = Modifier.padding(top = 4.dp, end = 4.dp, bottom = 4.dp),
            verticalAlignment = Alignment.CenterVertically
          ) {
            Text(
              text = formatTimestamp(entry.timestamp, zoneId, formatters),
              style = MaterialTheme.typography.labelSmall,
              color = colors.second.copy(alpha = 0.6f)
            )
            if (isFromMe) {
              Icon(
                imageVector = when (entry.status) {
                  MessageStatus.Read -> Icons.Filled.DoneAll
                  MessageStatus.Delivered -> Icons.Filled.Done
                  MessageStatus.Sending -> Icons.Filled.Schedule
                  else -> Icons.Filled.Schedule
                },
                contentDescription = stringResource(
                  id = R.string.message_status_description,
                  entry.status
                ),
                modifier = Modifier
                  .size(16.dp)
                  .padding(start = 4.dp),
                tint = if (entry.status == MessageStatus.Read) {
                  MaterialTheme.extendedColors.success
                } else {
                  colors.second.copy(alpha = 0.6f)
                }
              )
            }
          }
        }
      }
    }

    if (!isFromMe) {
      Row(verticalAlignment = Alignment.CenterVertically) {
        bubbleContent()
        AnimatedVisibility(
          visible = isUnread,
          enter = scaleIn() + fadeIn(),
          exit = scaleOut() + fadeOut()
        ) {
          Box(
            modifier = Modifier
              .padding(start = 8.dp)
              .size(4.dp)
              .background(MaterialTheme.colorScheme.primary, CircleShape)
          )
        }
      }
    } else {
      bubbleContent()
    }
  }
}
