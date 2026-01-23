package hela.eillance.viewmodels

import android.app.Application
import android.content.ContentResolver
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.ThumbnailUtils
import android.net.Uri
import android.os.Build
import android.os.CancellationSignal
import android.provider.MediaStore
import android.util.Base64
import android.util.Log
import android.util.Size
import android.webkit.MimeTypeMap
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
import java.io.FileOutputStream
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

        val cr = context.contentResolver
        val detectedMime = cr.getType(uri) ?: (if (type == AttachmentType.Image) "image/jpeg" else "video/mp4")
        
        val ext = MimeTypeMap.getSingleton().getExtensionFromMimeType(detectedMime) 
                  ?: (if (type == AttachmentType.Image) "jpg" else "mp4")
                  
        val fileName = "${UUID.randomUUID()}.$ext"
        val destFile = File(attachmentsDir, fileName)

        // Secure copy
        context.contentResolver.openInputStream(uri)?.use { input ->
          FileOutputStream(destFile).use { output ->
            input.copyTo(output)
          }
        } ?: throw Exception("Failed to open input stream for $uri")

        // Generate Thumbnail
        val thumbnailBase64 = try {
          val bitmap = generateThumbnail(context.contentResolver, uri, destFile, type)
          if (bitmap != null) {
            val outputStream = ByteArrayOutputStream()
            bitmap.compress(Bitmap.CompressFormat.JPEG, 70, outputStream)
            val bytes = outputStream.toByteArray()
            Base64.encodeToString(bytes, Base64.NO_WRAP)
          } else {
            ""
          }
        } catch (e: Exception) {
          Log.w("ConversationVM", "Failed to generate thumbnail for $uri", e)
          ""
        }

        val request = AttachmentRequest(
          path = destFile.absolutePath,
          type = type.value,
          thumbnail = thumbnailBase64,
          mimeType = detectedMime
        )

        if (nativePtr != 0L) {
          nativeSendMessageWithAttachments(nativePtr, "", arrayOf(request))
        }

      } catch (e: Exception) {
        Log.e("ConversationVM", "Failed to process attachment", e)
      }
    }
  }

  private fun generateThumbnail(resolver: ContentResolver, uri: Uri, file: File, type: AttachmentType): Bitmap? {
    val size = Size(320, 320)
    return try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        resolver.loadThumbnail(uri, size, CancellationSignal())
      } else {
        // Fallback for older versions
        if (type == AttachmentType.Video) {
          ThumbnailUtils.createVideoThumbnail(file.absolutePath, MediaStore.Images.Thumbnails.MINI_KIND)
        } else {
          // For images, we can just decode with scaling
          val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
          BitmapFactory.decodeFile(file.absolutePath, options)
          options.inSampleSize = calculateInSampleSize(options, 320, 320)
          options.inJustDecodeBounds = false
          BitmapFactory.decodeFile(file.absolutePath, options)
        }
      }
    } catch (e: Exception) {
      Log.e("ConversationVM", "Thumbnail generation failed", e)
      null
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
