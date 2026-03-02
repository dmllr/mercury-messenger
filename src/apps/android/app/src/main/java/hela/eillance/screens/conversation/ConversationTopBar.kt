package hela.eillance.screens.conversation

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Call
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.PermMedia
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.Contact

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SelectionTopBar(
  count: Int,
  onClear: () -> Unit,
  onCopy: () -> Unit
) {
  TopAppBar(
    title = {
      Text(
        text = stringResource(id = R.string.selected_count, count),
        style = MaterialTheme.typography.titleMedium
      )
    },
    navigationIcon = {
      IconButton(onClick = onClear) {
        Icon(Icons.Default.Close, contentDescription = stringResource(id = R.string.action_close))
      }
    },
    actions = {
      IconButton(onClick = onCopy) {
        Icon(
          Icons.Default.ContentCopy,
          contentDescription = stringResource(id = R.string.action_copy)
        )
      }
    },
    colors = TopAppBarDefaults.topAppBarColors(
      containerColor = MaterialTheme.colorScheme.surfaceContainerHigh
    )
  )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConversationDefaultTopBar(
  peerId: String,
  contact: Contact?,
  onBack: () -> Unit,
  onShowContactProfile: (String) -> Unit,
  onStartVoiceCall: () -> Unit,
  onStartVideoCall: () -> Unit,
  onMediaClick: () -> Unit,
  onSearchClick: () -> Unit
) {
  TopAppBar(
    title = {
      Row(
        modifier = Modifier.clickable { onShowContactProfile(peerId) },
        verticalAlignment = Alignment.CenterVertically
      ) {
        UserAvatar(
          photoBase64 = contact?.photo,
          modifier = Modifier.size(40.dp)
        )
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
        Icon(
          Icons.AutoMirrored.Filled.ArrowBack,
          contentDescription = stringResource(id = R.string.action_back)
        )
      }
    },
    actions = {
      IconButton(onClick = onMediaClick) {
        Icon(Icons.Default.PermMedia, contentDescription = stringResource(id = R.string.action_media))
      }
      IconButton(onClick = onSearchClick) {
        Icon(Icons.Default.Search, contentDescription = stringResource(R.string.action_search))
      }
      IconButton(onClick = onStartVoiceCall) {
        Icon(
          Icons.Default.Call,
          contentDescription = stringResource(id = R.string.action_voice_call)
        )
      }
      IconButton(onClick = onStartVideoCall) {
        Icon(
          Icons.Default.Videocam,
          contentDescription = stringResource(id = R.string.action_video_call)
        )
      }
    }
  )
}
