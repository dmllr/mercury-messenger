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
import androidx.compose.ui.unit.dp
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
        title = { Text("Unread Messages") },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
          }
        }
      )
    }
  ) { padding ->
    LazyColumn(modifier = Modifier.padding(padding)) {
      if (infos.isEmpty()) {
        item {
          Text("No unread messages found.", modifier = Modifier.padding(16.dp))
        }
      }
      items(infos) { info ->
        ListItem(
          headlineContent = { Text(info.peerId) },
          supportingContent = { Text("Last Msg: ${Date(info.lastMsgTimestamp / 1000)}") },
          trailingContent = { Text("${info.count} unread") }
        )
        HorizontalDivider()
      }
    }
  }
}
