package hela.eillance.screens.conversation

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Contact

@Composable
fun PendingApprovalBar(
  contact: Contact,
  onCancel: () -> Unit,
  onResend: () -> Unit
) {
  Surface(
    modifier = Modifier.fillMaxWidth(),
    tonalElevation = 3.dp
  ) {
    Column(
      modifier = Modifier
        .padding(16.dp)
        .navigationBarsPadding(),
      horizontalAlignment = Alignment.CenterHorizontally
    ) {
      Text(
        text = stringResource(id = R.string.contact_approval_waiting_for_peer, contact.name),
        style = MaterialTheme.typography.titleMedium,
        textAlign = TextAlign.Center
      )
      Spacer(modifier = Modifier.height(16.dp))
      Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly
      ) {
        OutlinedButton(onClick = onCancel) { Text(stringResource(id = R.string.action_cancel_request)) }
        Button(onClick = onResend) { Text(stringResource(id = R.string.action_resend_request)) }
      }
    }
  }
}

@Composable
fun ApprovalRequestBar(
  contact: Contact,
  onApprove: () -> Unit,
  onReject: () -> Unit
) {
  Surface(
    modifier = Modifier.fillMaxWidth(),
    tonalElevation = 3.dp
  ) {
    Column(
      modifier = Modifier
        .padding(16.dp)
        .navigationBarsPadding(),
      horizontalAlignment = Alignment.CenterHorizontally
    ) {
      Text(
        text = stringResource(id = R.string.contact_approval_request_from_peer, contact.name),
        style = MaterialTheme.typography.titleMedium,
        textAlign = TextAlign.Center
      )
      if (!contact.bio.isNullOrBlank()) {
        Spacer(modifier = Modifier.height(8.dp))
        Text(
          text = "\"${contact.bio}\"",
          style = MaterialTheme.typography.bodyMedium,
          fontStyle = FontStyle.Italic,
          textAlign = TextAlign.Center,
          color = MaterialTheme.colorScheme.onSurfaceVariant
        )
      }
      Spacer(modifier = Modifier.height(16.dp))
      Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly
      ) {
        OutlinedButton(onClick = onReject) { Text(stringResource(id = R.string.action_reject)) }
        Button(onClick = onApprove) { Text(stringResource(id = R.string.action_approve_contact)) }
      }
    }
  }
}
