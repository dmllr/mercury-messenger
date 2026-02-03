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
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import hela.eillance.ndkio.AttachmentRequest
import hela.eillance.ndkio.AttachmentStatus
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.HistoryPage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileOutputStream

data class DraftAttachment(
  val uri: Uri,
  val type: AttachmentType,
  val mimeType: String,
  val name: String, // Original filename
  val bitmap: Bitmap? = null,
  var note: String = ""
)

class ConversationViewModel(
  application: Application,
  private val peerId: String
) : AndroidViewModel(application), NativeConversationObserver {

  private var nativePtr: Long = 0
  private val _interactionHistoryPage = MutableStateFlow(HistoryPage())
  val interactionHistoryPage = _interactionHistoryPage.asStateFlow()

  private val _transferProgress = MutableStateFlow<Map<String, Float>>(emptyMap())
  val transferProgress = _transferProgress.asStateFlow()

  private val _draftAttachments = MutableStateFlow<List<DraftAttachment>>(emptyList())
  val draftAttachments = _draftAttachments.asStateFlow()

  init {
    nativePtr = nativeCreate(peerId, this)
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

    val currentProgress = _transferProgress.value.toMutableMap()
    if (currentProgress.isNotEmpty()) {
      val activeAttachmentIds = mutableSetOf<String>()
      for (msg in page.messages) {
        for (att in msg.attachments) {
          if (att.status == AttachmentStatus.Transferring) {
            activeAttachmentIds.add(att.id)
          }
        }
      }

      val toRemove = currentProgress.keys.filter { it !in activeAttachmentIds }
      if (toRemove.isNotEmpty()) {
        for (id in toRemove) currentProgress.remove(id)
        _transferProgress.value = currentProgress
      }
    }
  }

  override fun onTransferProgress(attachmentId: String, progress: Float) {
    val currentMap = _transferProgress.value.toMutableMap()
    if (progress >= 1.0f) {
      currentMap.remove(attachmentId)
    } else {
      currentMap[attachmentId] = progress
    }
    _transferProgress.value = currentMap
  }

  // --- Draft Management ---

  fun addDraftAttachment(uri: Uri, isCamera: Boolean = false) {
    viewModelScope.launch(Dispatchers.IO) {
      val context = getApplication<Application>()
      val cr = context.contentResolver
      val mimeTypeRaw = cr.getType(uri) ?: "application/octet-stream"

      val type = when {
        mimeTypeRaw.startsWith("video/") -> AttachmentType.Video
        mimeTypeRaw.startsWith("image/") -> AttachmentType.Image
        else -> AttachmentType.File
      }

      val name = queryFileName(cr, uri) ?: "file_${System.currentTimeMillis()}"

      val preview = if (type == AttachmentType.Image) {
        try {
          val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
          val streamCheck = cr.openInputStream(uri)
          BitmapFactory.decodeStream(streamCheck, null, options)
          streamCheck?.close()

          options.inSampleSize = calculateInSampleSize(options, 200, 200)
          options.inJustDecodeBounds = false
          val streamDecode = cr.openInputStream(uri)
          val bmp = BitmapFactory.decodeStream(streamDecode, null, options)
          streamDecode?.close()
          bmp
        } catch (e: Exception) { null }
      } else null

      val draft = DraftAttachment(uri, type, mimeTypeRaw, name, preview)

      val current = _draftAttachments.value.toMutableList()
      current.add(draft)
      _draftAttachments.value = current
    }
  }

  fun removeDraft(index: Int) {
    val current = _draftAttachments.value.toMutableList()
    if (index in current.indices) {
      current.removeAt(index)
      _draftAttachments.value = current
    }
  }

  fun updateDraftNote(index: Int, note: String) {
    val current = _draftAttachments.value.toMutableList()
    if (index in current.indices) {
      val draft = current[index]
      val truncated = if (note.length > 120) note.substring(0, 120) else note
      current[index] = draft.copy(note = truncated)
      _draftAttachments.value = current
    }
  }

  fun clearDrafts() {
    _draftAttachments.value = emptyList()
  }

  private fun queryFileName(resolver: ContentResolver, uri: Uri): String? {
    val returnCursor = resolver.query(uri, null, null, null, null)
    returnCursor?.use { cursor ->
      val nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
      if (nameIndex != -1) {
        cursor.moveToFirst()
        return cursor.getString(nameIndex)
      }
    }
    return null
  }

  private fun resolveUniqueFile(dir: File, fileName: String): File {
    var file = File(dir, fileName)
    if (!file.exists()) return file

    val name = file.nameWithoutExtension
    val ext = file.extension
    var counter = 1
    while (file.exists()) {
      file = File(dir, "$name($counter).$ext")
      counter++
    }
    return file
  }

  // --- Sending ---

  fun sendMessage(messageText: String) {
    val drafts = _draftAttachments.value
    if (messageText.isBlank() && drafts.isEmpty()) return

    if (drafts.isEmpty()) {
      if (nativePtr != 0L) {
        nativeSendMessage(nativePtr, messageText)
      }
    } else {
      processAndSend(messageText, drafts)
    }
  }

  private fun processAndSend(text: String, drafts: List<DraftAttachment>) {
    _draftAttachments.value = emptyList()

    viewModelScope.launch(Dispatchers.IO) {
      val attachmentRequests = mutableListOf<AttachmentRequest>()
      val context = getApplication<Application>()

      // Use cacheDir for sending to avoid duplicating storage permanently.
      // C++ will read from here. Android OS cleans this up eventually.
      val cacheDir = context.cacheDir
      if (!cacheDir.exists()) cacheDir.mkdirs()

      for (draft in drafts) {
        try {
          // Resolve unique filename in cache to prevent collisions for this batch
          val destFile = resolveUniqueFile(cacheDir, draft.name)

          // Copy content to cache so C++ ifstream can read it (C++ cannot read content:// Uris directly)
          context.contentResolver.openInputStream(draft.uri)?.use { input ->
            FileOutputStream(destFile).use { output ->
              input.copyTo(output)
            }
          } ?: throw Exception("Failed to open input stream for ${draft.uri}")

          val thumbnailBase64 = try {
            val bitmap = generateThumbnail(context.contentResolver, draft.uri, destFile, draft.type)
            if (bitmap != null) {
              val outputStream = ByteArrayOutputStream()
              bitmap.compress(Bitmap.CompressFormat.JPEG, 60, outputStream)
              val bytes = outputStream.toByteArray()
              Base64.encodeToString(bytes, Base64.NO_WRAP)
            } else ""
          } catch (e: Exception) { "" }

          val request = AttachmentRequest(
            path = destFile.absolutePath,
            type = draft.type.value,
            thumbnail = thumbnailBase64,
            mimeType = draft.mimeType,
            note = draft.note,
            fileName = draft.name // Pass the original filename to be sent in protocol
          )
          attachmentRequests.add(request)

        } catch (e: Exception) {
          Log.e("ConversationVM", "Failed to process attachment draft", e)
        }
      }

      if (nativePtr != 0L) {
        nativeSendMessageWithAttachments(nativePtr, text, attachmentRequests.toTypedArray())
      }
    }
  }

  fun sendMedia(uris: List<Uri>) {
    for (uri in uris) {
      addDraftAttachment(uri)
    }
  }

  fun cancelTransfer(attachmentId: String, isUpload: Boolean) {
    if (nativePtr != 0L) nativeCancelTransfer(nativePtr, attachmentId, isUpload)
  }

  fun resumeTransfer(attachmentId: String, isUpload: Boolean) {
    if (nativePtr != 0L) nativeResumeTransfer(nativePtr, attachmentId, isUpload)
  }

  fun discardAttachment(attachmentId: String) {
    if (nativePtr != 0L) nativeDiscardAttachment(nativePtr, attachmentId)
  }

  private fun generateThumbnail(resolver: ContentResolver, uri: Uri, file: File, type: AttachmentType): Bitmap? {
    val size = Size(320, 320)
    return try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        resolver.loadThumbnail(uri, size, CancellationSignal())
      } else {
        if (type == AttachmentType.Video) {
          ThumbnailUtils.createVideoThumbnail(file.absolutePath, MediaStore.Images.Thumbnails.MINI_KIND)
        } else if (type == AttachmentType.Image) {
          val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
          BitmapFactory.decodeFile(file.absolutePath, options)
          options.inSampleSize = calculateInSampleSize(options, 320, 320)
          options.inJustDecodeBounds = false
          BitmapFactory.decodeFile(file.absolutePath, options)
        } else {
          null
        }
      }
    } catch (e: Exception) { null }
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

  fun loadOlderMessages() { if (nativePtr != 0L) nativeLoadOlder(nativePtr) }
  fun loadFullMessage(messageId: Long) { if (nativePtr != 0L) nativeLoadFull(nativePtr, messageId) }
  fun markMessagesAsRead(messageIds: List<Long>) { if (nativePtr != 0L && messageIds.isNotEmpty()) nativeMarkRead(nativePtr, messageIds.toLongArray()) }

  private external fun nativeCreate(peerId: String, observer: NativeConversationObserver): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeSendMessage(ptr: Long, message: String)
  private external fun nativeSendMessageWithAttachments(ptr: Long, message: String, attachments: Array<AttachmentRequest>)
  private external fun nativeLoadOlder(ptr: Long)
  private external fun nativeLoadFull(ptr: Long, messageId: Long)
  private external fun nativeMarkRead(ptr: Long, messageIds: LongArray)
  private external fun nativeCancelTransfer(ptr: Long, attachmentId: String, isUpload: Boolean)
  private external fun nativeResumeTransfer(ptr: Long, attachmentId: String, isUpload: Boolean)
  private external fun nativeDiscardAttachment(ptr: Long, attachmentId: String)

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
