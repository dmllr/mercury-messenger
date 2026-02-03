package hela.eillance.screens

import android.util.Log
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import hela.eillance.camera.R
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.utils.rememberImageBitmapFromBase64
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

@Serializable
private data class ProfileUpdateAction(
  val action: String,
  val old: ProfileData? = null,
  val new: ProfileData? = null
) {
  @Serializable
  data class ProfileData(
    @SerialName("display_name") val displayName: String? = null,
    val bio: String? = null,
    val photo: String? = null
  )
}

@Composable
fun SystemMessageDetailsDialog(entry: HistoryEntry, onDismiss: () -> Unit) {
  val json = Json { ignoreUnknownKeys = true }
  val action = remember(entry.content) {
    entry.content?.let {
      try {
        json.decodeFromString<ProfileUpdateAction>(it)
      } catch (e: Exception) {
        // Log the error for debugging, but don't crash.
        Log.w("SystemMessageDialog", "Failed to parse system message content", e)
        null
      }
    }
  }

  if (action?.action == "profile_update_diff") {
    ProfileUpdateDialog(action = action, onDismiss = onDismiss)
  }
}

@Composable
private fun ProfileUpdateDialog(action: ProfileUpdateAction, onDismiss: () -> Unit) {
  Dialog(onDismissRequest = onDismiss) {
    Card(
      modifier = Modifier.fillMaxWidth(),
      shape = MaterialTheme.shapes.large,
    ) {
      Column(
        modifier = Modifier
          .padding(24.dp)
          .verticalScroll(rememberScrollState())
      ) {
        Text(
          stringResource(id = R.string.profile_update_dialog_title),
          style = MaterialTheme.typography.headlineSmall,
          modifier = Modifier.padding(bottom = 16.dp)
        )

        ComparePhotos(action.old?.photo, action.new?.photo)
        CompareField(
          stringResource(id = R.string.profile_update_field_display_name),
          action.old?.displayName,
          action.new?.displayName
        )
        CompareField(stringResource(id = R.string.profile_update_field_bio), action.old?.bio, action.new?.bio)

        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
          TextButton(onClick = onDismiss) {
            Text(stringResource(id = R.string.action_close))
          }
        }
      }
    }
  }
}

@Composable
private fun ComparePhotos(oldPhoto: String?, newPhoto: String?) {
  if (oldPhoto == newPhoto) return

  Column(modifier = Modifier.fillMaxWidth()) {
    Text(
      stringResource(id = R.string.profile_update_field_photo),
      style = MaterialTheme.typography.titleMedium,
      color = MaterialTheme.colorScheme.primary
    )
    Spacer(Modifier.height(8.dp))
    Row(
      modifier = Modifier.fillMaxWidth(),
      horizontalArrangement = Arrangement.SpaceEvenly,
      verticalAlignment = Alignment.CenterVertically
    ) {
      ProfilePhoto(photoBase64 = oldPhoto, modifier = Modifier.weight(1f))
      Icon(
        Icons.AutoMirrored.Filled.ArrowForward,
        contentDescription = stringResource(id = R.string.changed_to_description),
        modifier = Modifier.padding(horizontal = 8.dp)
      )
      ProfilePhoto(photoBase64 = newPhoto, modifier = Modifier.weight(1f))
    }
    HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp))
  }
}

@Composable
private fun CompareField(label: String, oldValue: String?, newValue: String?) {
  val old = oldValue.orEmpty()
  val new = newValue.orEmpty()
  if (old == new) return

  Column(modifier = Modifier.fillMaxWidth()) {
    Text(label, style = MaterialTheme.typography.titleMedium, color = MaterialTheme.colorScheme.primary)
    Spacer(Modifier.height(8.dp))
    DiffText(label = stringResource(id = R.string.from_label), value = old)
    DiffText(label = stringResource(id = R.string.to_label), value = new)
    HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp))
  }
}

@Composable
private fun DiffText(label: String, value: String) {
  Row {
    Text(
      "$label: ",
      style = MaterialTheme.typography.bodyMedium,
      color = MaterialTheme.colorScheme.onSurfaceVariant
    )
    if (value.isNotEmpty()) {
      Text(value, style = MaterialTheme.typography.bodyMedium)
    } else {
      Text(
        "Not set",
        style = MaterialTheme.typography.bodyMedium,
        fontStyle = FontStyle.Italic,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
    }
  }
}


@Composable
private fun ProfilePhoto(photoBase64: String?, modifier: Modifier = Modifier) {
  Box(
    modifier = modifier
      .aspectRatio(1f)
      .clip(CircleShape)
      .background(MaterialTheme.colorScheme.surfaceContainer)
      .border(1.dp, MaterialTheme.colorScheme.outlineVariant, CircleShape),
    contentAlignment = Alignment.Center
  ) {
    val imageBitmap = rememberImageBitmapFromBase64(photoBase64)
    if (imageBitmap != null) {
      Image(
        bitmap = imageBitmap,
        contentDescription = stringResource(id = R.string.profile_update_field_photo),
        modifier = Modifier.matchParentSize(),
        contentScale = ContentScale.Crop
      )
    } else {
      Text(
        stringResource(id = R.string.no_photo),
        textAlign = TextAlign.Center,
        style = MaterialTheme.typography.bodySmall
      )
    }
  }
}
