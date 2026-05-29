package hela.eillance.screens.conversation

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R

@Composable
fun ChannelReadOnlyBar() {
  Surface(modifier = Modifier.fillMaxWidth(), tonalElevation = 3.dp) {
    Box(modifier = Modifier.padding(16.dp).navigationBarsPadding(), contentAlignment = Alignment.Center) {
      Text(
        text = stringResource(id = R.string.channel_read_only),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
    }
  }
}
