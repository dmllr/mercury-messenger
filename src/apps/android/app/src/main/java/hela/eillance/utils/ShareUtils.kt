package hela.eillance.utils

import android.content.Context
import android.content.Intent
import androidx.core.content.FileProvider
import hela.eillance.ndkio.Attachment
import java.io.File

fun shareContent(context: Context, text: String?, attachments: List<Attachment>) {
  val uris = attachments.filter { it.isDownloaded }.map {
    FileProvider.getUriForFile(context, "${context.packageName}.provider", File(it.path))
  }
  val intent = Intent().apply {
    if (uris.isEmpty()) {
      action = Intent.ACTION_SEND
      type = "text/plain"
      putExtra(Intent.EXTRA_TEXT, text)
    } else if (uris.size == 1) {
      action = Intent.ACTION_SEND
      type = attachments.first().mime
      putExtra(Intent.EXTRA_STREAM, uris.first())
      if (!text.isNullOrBlank()) putExtra(Intent.EXTRA_TEXT, text)
      addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    } else {
      action = Intent.ACTION_SEND_MULTIPLE
      type = "*/*"
      putParcelableArrayListExtra(Intent.EXTRA_STREAM, ArrayList(uris))
      if (!text.isNullOrBlank()) putExtra(Intent.EXTRA_TEXT, text)
      addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
  }
  context.startActivity(Intent.createChooser(intent, null))
}
