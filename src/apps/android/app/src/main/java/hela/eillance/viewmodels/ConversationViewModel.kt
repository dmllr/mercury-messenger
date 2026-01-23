package hela.eillance.viewmodels

import android.app.Application
import android.net.Uri
import android.util.Log
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
        // Create attachments directory if it doesn't exist
        val attachmentsDir = File(context.filesDir, "attachments")
        if (!attachmentsDir.exists()) {
          attachmentsDir.mkdirs()
        }

        // Generate a unique filename
        val ext = if (type == AttachmentType.Image) "jpg" else "mp4" // Simple extension logic
        val fileName = "${UUID.randomUUID()}.$ext"
        val destFile = File(attachmentsDir, fileName)

        // Copy file from ContentProvider to Internal Storage
        context.contentResolver.openInputStream(uri)?.use { input ->
          destFile.outputStream().use { output ->
            input.copyTo(output)
          }
        }

        // Generate thumbnail (Placeholder for now, implementation omitted for brevity)
        val thumbnailBase64 = ""

        val request = AttachmentRequest(
          path = destFile.absolutePath,
          type = type.value,
          thumbnail = thumbnailBase64
        )

        // Send message with empty text and 1 attachment
        if (nativePtr != 0L) {
          nativeSendMessageWithAttachments(nativePtr, "", arrayOf(request))
        }

      } catch (e: Exception) {
        Log.e("ConversationVM", "Failed to process attachment", e)
      }
    }
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

  // Factory to pass peerId
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
