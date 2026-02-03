package hela.eillance.screens.debug

import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.DebugUnreadInfo
import hela.eillance.viewmodels.DebugViewModel
import java.util.Date

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun UnreadMessagesScreen(
  debugVM: DebugViewModel,
  onBack: () -> Unit
) {
  var infos by remember { mutableStateOf<List<DebugUnreadInfo>>(emptyList()) }

  LaunchedEffect(Unit) {
    infos = debugVM.getUnreadInfo()
  }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_unread_messages)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(id = R.string.action_back))
          }
        }
      )
    }
  ) { padding ->
    LazyColumn(modifier = Modifier.padding(padding)) {
      if (infos.isEmpty()) {
        item {
          Text(stringResource(id = R.string.debug_unread_empty), modifier = Modifier.padding(16.dp))
        }
      }
      items(infos) { info ->
        ListItem(
          headlineContent = { Text(info.peerId) },
          supportingContent = { Text(stringResource(id = R.string.debug_unread_last_message, Date(info.lastMsgTimestamp / 1000).toString())) },
          trailingContent = { Text(stringResource(id = R.string.debug_unread_count, info.count)) }
        )
        HorizontalDivider()
      }
    }
  }
}
