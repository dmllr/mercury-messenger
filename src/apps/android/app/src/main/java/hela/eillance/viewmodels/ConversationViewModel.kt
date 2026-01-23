package hela.eillance.viewmodels

import android.app.Application
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.ThumbnailUtils
import android.net.Uri
import android.provider.MediaStore
import android.util.Base64
import android.util.Log
import android.util.Size
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.AttachmentRequest
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.HistoryPage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.UUID

class ConversationViewModel(
  application: Application,
  private val peerId: String
) : AndroidViewModel(application), NativeConversationObserver {

  private var nativePtr: Long = 0
  private val _interactionHistoryPage = MutableStateFlow(HistoryPage())
  val interactionHistoryPage = _interactionHistoryPage.asStateFlow()

  init {
    nativePtr = nativeCreate(peerId, this)
    if (nativePtr == 0L) {
      Log.e("ConversationViewModel", "Failed to create native ConversationViewModel for $peerId")
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  override fun onHistoryUpdated(page: HistoryPage) {
    _interactionHistoryPage.value = page
  }

  fun sendMessage(message: String) {
    if (nativePtr != 0L && message.isNotBlank()) {
      nativeSendMessage(nativePtr, message)
    }
  }

  fun sendImage(uri: Uri) {
    processAndSendAttachment(uri, AttachmentType.Image)
  }

  fun sendVideo(uri: Uri) {
    processAndSendAttachment(uri, AttachmentType.Video)
  }

  private fun processAndSendAttachment(uri: Uri, type: AttachmentType) {
    viewModelScope.launch(Dispatchers.IO) {
      try {
        val context = getApplication<Application>()
        val attachmentsDir = File(context.filesDir, "attachments")
        if (!attachmentsDir.exists()) {
          attachmentsDir.mkdirs()
        }

        val ext = if (type == AttachmentType.Image) "jpg" else "mp4"
        val fileName = "${UUID.randomUUID()}.$ext"
        val destFile = File(attachmentsDir, fileName)

        context.contentResolver.openInputStream(uri)?.use { input ->
          destFile.outputStream().use { output ->
            input.copyTo(output)
          }
        }

        // Generate Thumbnail
        val thumbnailBase64 = try {
          val bitmap = if (type == AttachmentType.Video) {
            // For video, createVideoThumbnail is deprecated in API 29, but simplifies this snippet.
            // Using ThumbnailUtils for broader compatibility or ContentResolver for API 29+ is preferred.
            // Simple fallback approach:
            try {
              if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.Q) {
                context.contentResolver.loadThumbnail(uri, Size(320, 320), null)
              } else {
                ThumbnailUtils.createVideoThumbnail(destFile.absolutePath, MediaStore.Images.Thumbnails.MINI_KIND)
              }
            } catch (e: Exception) { null }
          } else {
            // Image resizing
            val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(destFile.absolutePath, options)
            options.inSampleSize = calculateInSampleSize(options, 320, 320)
            options.inJustDecodeBounds = false
            BitmapFactory.decodeFile(destFile.absolutePath, options)
          }

          if (bitmap != null) {
            val outputStream = ByteArrayOutputStream()
            bitmap.compress(Bitmap.CompressFormat.JPEG, 70, outputStream)
            Base64.encodeToString(outputStream.toByteArray(), Base64.NO_WRAP)
          } else {
            ""
          }
        } catch (e: Exception) {
          Log.w("ConversationVM", "Failed to generate thumbnail", e)
          ""
        }

        val request = AttachmentRequest(
          path = destFile.absolutePath,
          type = type.value,
          thumbnail = thumbnailBase64
        )

        if (nativePtr != 0L) {
          nativeSendMessageWithAttachments(nativePtr, "", arrayOf(request))
        }

      } catch (e: Exception) {
        Log.e("ConversationVM", "Failed to process attachment", e)
      }
    }
  }

  private fun calculateInSampleSize(options: BitmapFactory.Options, reqWidth: Int, reqHeight: Int): Int {
    val (height: Int, width: Int) = options.outHeight to options.outWidth
    var inSampleSize = 1
    if (height > reqHeight || width > reqWidth) {
      val halfHeight: Int = height / 2
      val halfWidth: Int = width / 2
      while ((halfHeight / inSampleSize) >= reqHeight && (halfWidth / inSampleSize) >= reqWidth) {
        inSampleSize *= 2
      }
    }
    return inSampleSize
  }

  fun loadOlderMessages() {
    if (nativePtr != 0L) nativeLoadOlder(nativePtr)
  }

  fun loadFullMessage(messageId: Long) {
    if (nativePtr != 0L) nativeLoadFull(nativePtr, messageId)
  }

  fun markMessagesAsRead(messageIds: List<Long>) {
    if (nativePtr != 0L && messageIds.isNotEmpty()) {
      nativeMarkRead(nativePtr, messageIds.toLongArray())
    }
  }

  // --- JNI Definitions ---
  private external fun nativeCreate(peerId: String, observer: NativeConversationObserver): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeSendMessage(ptr: Long, message: String)
  private external fun nativeSendMessageWithAttachments(ptr: Long, message: String, attachments: Array<AttachmentRequest>)
  private external fun nativeLoadOlder(ptr: Long)
  private external fun nativeLoadFull(ptr: Long, messageId: Long)
  private external fun nativeMarkRead(ptr: Long, messageIds: LongArray)

  class Factory(
    private val application: Application,
    private val peerId: String
  ) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
      return ConversationViewModel(application, peerId) as T
    }
  }
}
