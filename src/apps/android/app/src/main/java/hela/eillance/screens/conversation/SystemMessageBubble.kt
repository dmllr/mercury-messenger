package hela.eillance.screens.conversation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.MessageDirection
import hela.eillance.localization.LocalizationMapping
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

private val jsonParser = Json { ignoreUnknownKeys = true }

@Serializable
private data class JsonAction(val action: String)

@Composable
fun SystemMessageBubble(
  entry: HistoryEntry,
  onClick: (HistoryEntry) -> Unit,
  isUnread: Boolean = false
) {
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

  val textResId = LocalizationMapping.getSystemMessageResId(parsedAction?.action)

  val finalResId =
    if (parsedAction?.action == "contact_confirm" && entry.direction == MessageDirection.Sent) {
      textResId
    } else {
      textResId
    }

  if (parsedAction == null && entry.content.isNullOrBlank()) return

  val isClickable = parsedAction?.action == "profile_update_diff"

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .padding(vertical = 8.dp)
      .clip(MaterialTheme.shapes.small)
      .clickable(enabled = isClickable) { onClick(entry) }
      .padding(horizontal = 16.dp),
    horizontalArrangement = Arrangement.Center,
    verticalAlignment = Alignment.CenterVertically
  ) {
    Box(
      modifier = Modifier
        .clip(RoundedCornerShape(12.dp))
        .background(MaterialTheme.colorScheme.surfaceContainer)
        .padding(horizontal = 12.dp, vertical = 6.dp)
    ) {
      Text(
        text = stringResource(id = finalResId),
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
    }
    AnimatedVisibility(
      visible = isUnread,
      enter = scaleIn() + fadeIn(),
      exit = scaleOut() + fadeOut()
    ) {
      Box(
        modifier = Modifier
          .padding(start = 8.dp)
          .size(6.dp)
          .background(MaterialTheme.colorScheme.primary, CircleShape)
      )
    }
  }
}
