package hela.eillance.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp

@Composable
fun StatusSnackbar(
  text: String,
  actionLabel: String? = null,
  onAction: (() -> Unit)? = null,
  dismissLabel: String? = null,
  onDismiss: (() -> Unit)? = null,
  isError: Boolean = false
) {
  val backgroundColor = if (isError) MaterialTheme.colorScheme.errorContainer else MaterialTheme.colorScheme.inverseSurface
  val contentColor = if (isError) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.inverseOnSurface
  val actionColor = if (isError) MaterialTheme.colorScheme.onErrorContainer else MaterialTheme.colorScheme.primaryContainer

  Row(
    modifier = Modifier
      .fillMaxWidth()
      .clip(RoundedCornerShape(8.dp))
      .background(backgroundColor)
      .padding(horizontal = 16.dp, vertical = 12.dp),
    verticalAlignment = Alignment.CenterVertically,
    horizontalArrangement = Arrangement.SpaceBetween
  ) {
    Text(
      text = text,
      color = contentColor,
      style = MaterialTheme.typography.bodyMedium,
      modifier = Modifier.weight(1f)
    )

    if (onAction != null && actionLabel != null) {
      Spacer(modifier = Modifier.width(8.dp))
      TextButton(
        onClick = onAction,
        colors = ButtonDefaults.textButtonColors(contentColor = actionColor)
      ) {
        Text(actionLabel)
      }
    }

    if (onDismiss != null && dismissLabel != null) {
      TextButton(
        onClick = onDismiss,
        colors = ButtonDefaults.textButtonColors(contentColor = contentColor.copy(alpha = 0.7f))
      ) {
        Text(dismissLabel)
      }
    }
  }
}
