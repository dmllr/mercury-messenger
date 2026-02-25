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
import hela.eillance.theme.ListItem
import androidx.compose.material3.MaterialTheme
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
import hela.eillance.ndkio.DebugPendingAction
import hela.eillance.viewmodels.DebugViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PendingActionsScreen(
  debugVM: DebugViewModel,
  onBack: () -> Unit
) {
  var actions by remember { mutableStateOf<List<DebugPendingAction>>(emptyList()) }

  LaunchedEffect(Unit) {
    actions = debugVM.getPendingActions()
  }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_pending_actions)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(
              Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = stringResource(id = R.string.action_back)
            )
          }
        }
      )
    }
  ) { padding ->
    LazyColumn(modifier = Modifier.padding(padding)) {
      if (actions.isEmpty()) {
        item {
          Text(
            stringResource(id = R.string.debug_pending_actions_empty),
            modifier = Modifier.padding(16.dp)
          )
        }
      }
      items(actions) { action ->
        ListItem(
          headlineContent = { Text(action.peerId.take(8) + "...") },
          supportingContent = {
            Text(action.payload, style = MaterialTheme.typography.bodySmall, maxLines = 10)
          },
          overlineContent = {
            Text(
              stringResource(
                id = R.string.debug_pending_action_id,
                action.id
              )
            )
          }
        )
        HorizontalDivider()
      }
    }
  }
}
