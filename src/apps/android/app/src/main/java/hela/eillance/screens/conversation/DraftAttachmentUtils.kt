package hela.eillance.screens.conversation

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.SnippetFolder
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.ui.graphics.vector.ImageVector

internal fun getDraftIcon(mimeType: String): ImageVector {
  return when {
    mimeType == "application/pdf" -> Icons.Default.Description
    mimeType.startsWith("audio/") -> Icons.Default.AudioFile
    mimeType.startsWith("video/") -> Icons.Default.Videocam
    mimeType.startsWith("image/") -> Icons.Default.Image
    else -> Icons.Default.SnippetFolder
  }
}
