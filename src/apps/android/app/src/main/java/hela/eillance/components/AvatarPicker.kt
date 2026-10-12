package hela.eillance.components

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Base64
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import hela.eillance.screens.profile.ImageEditorDialog
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream

@Composable
fun AvatarPicker(
  hasPhoto: Boolean,
  onPhotoPicked: (String) -> Unit,
  onPhotoCleared: (() -> Unit)? = null,
  modifier: Modifier = Modifier,
  fallbackContent: @Composable () -> Unit
) {
  var rawSelectedImage by remember { mutableStateOf<Bitmap?>(null) }
  var showImageEditor by remember { mutableStateOf(false) }
  val context = LocalContext.current
  val scope = rememberCoroutineScope()

  val imagePickerLauncher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.GetContent()
  ) { uri ->
    uri?.let {
      scope.launch(Dispatchers.IO) {
        try {
          context.contentResolver.openInputStream(it)?.use { inputStream ->
            val bitmap = BitmapFactory.decodeStream(inputStream)
            if (bitmap != null) {
              withContext(Dispatchers.Main) {
                rawSelectedImage = bitmap
                showImageEditor = true
              }
            }
          }
        } catch (e: Exception) {
          e.printStackTrace()
        }
      }
    }
  }

  if (showImageEditor && rawSelectedImage != null) {
    ImageEditorDialog(
      bitmap = rawSelectedImage!!,
      onDismiss = {
        showImageEditor = false
        rawSelectedImage = null
      },
      onSave = { croppedBitmap ->
        scope.launch(Dispatchers.Default) {
          val outputStream = ByteArrayOutputStream()
          croppedBitmap.compress(Bitmap.CompressFormat.JPEG, 85, outputStream)
          val byteArray = outputStream.toByteArray()
          val base64 = Base64.encodeToString(byteArray, Base64.NO_WRAP)
          withContext(Dispatchers.Main) {
            onPhotoPicked(base64)
            showImageEditor = false
            rawSelectedImage = null
          }
        }
      }
    )
  }

  Box(modifier = modifier) {
    Box(
      modifier = Modifier
        .matchParentSize()
        .clip(CircleShape)
        .clickable { imagePickerLauncher.launch("image/*") },
      contentAlignment = Alignment.Center
    ) {
      fallbackContent()
      Box(
        modifier = Modifier
          .fillMaxSize()
          .background(Color.Black.copy(alpha = 0.3f)),
        contentAlignment = Alignment.Center
      ) {
        Icon(Icons.Default.CameraAlt, contentDescription = "Edit", tint = Color.White)
      }
    }

    if (hasPhoto && onPhotoCleared != null) {
      IconButton(
        onClick = onPhotoCleared,
        modifier = Modifier
          .align(Alignment.TopEnd)
          .size(28.dp)
          .background(MaterialTheme.colorScheme.error, CircleShape)
          .padding(4.dp)
      ) {
        Icon(Icons.Default.Close, contentDescription = "Clear", tint = Color.White, modifier = Modifier.size(16.dp))
      }
    }
  }
}
