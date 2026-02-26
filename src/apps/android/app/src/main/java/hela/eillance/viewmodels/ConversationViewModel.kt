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
import hela.eillance.events.NativeEventBus
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentRequest
import hela.eillance.ndkio.AttachmentStatus
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.HistoryPage
import hela.eillance.ndkio.Reaction
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.security.MessageDigest

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
) : AndroidViewModel(application) {

  private var nativePtr: Long = 0
  private val _interactionHistoryPage = MutableStateFlow(HistoryPage())
  val interactionHistoryPage = _interactionHistoryPage.asStateFlow()

  private val _transferProgress = MutableStateFlow<Map<String, Float>>(emptyMap())
  val transferProgress = _transferProgress.asStateFlow()

  private val _draftAttachments = MutableStateFlow<List<DraftAttachment>>(emptyList())
  val draftAttachments = _draftAttachments.asStateFlow()

  private val _messageText = MutableStateFlow("")
  val messageText = _messageText.asStateFlow()

  private val _searchResults = MutableStateFlow<List<HistoryEntry>>(emptyList())
  val searchResults = _searchResults.asStateFlow()

  private val readQueue = mutableSetOf<Long>()
  private var readDebounceJob: Job? = null

  /**
   * Called when a message is rendered on screen.
   * Skip memory-only IDs (fake IDs generated for ephemeral system messages).
   */
  fun onMessageShown(messageId: Long) {
    // Memory-only IDs are high values (>= 10^18) or negative when interpreted as Long.
    // Real DB IDs are positive sequential integers.
    if (messageId !in 0..1_000_000_000_000_000L) return

    synchronized(readQueue) {
      readQueue.add(messageId)
    }

    if (readDebounceJob?.isActive != true) {
      readDebounceJob = viewModelScope.launch(Dispatchers.IO) {
        delay(250)
        val ids = synchronized(readQueue) {
          val ids = readQueue.toList()
          readQueue.clear()
          ids
        }
        if (ids.isNotEmpty()) {
          markMessagesAsRead(ids)
        }
      }
    }
  }

  init {
    nativePtr = nativeCreate(peerId)

    viewModelScope.launch {
      NativeEventBus.historyUpdates.collect { (pid, page) ->
        if (pid == peerId) {
          onHistoryUpdated(page)
        }
      }
    }
    viewModelScope.launch {
      NativeEventBus.transferProgress.collect { (pid, attId, progress) ->
        if (pid == peerId) {
          onTransferProgress(attId, progress)
        }
      }
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  private fun onHistoryUpdated(page: HistoryPage) {
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

  private fun onTransferProgress(attachmentId: String, progress: Float) {
    val currentMap = _transferProgress.value.toMutableMap()
    if (progress >= 1.0f) {
      currentMap.remove(attachmentId)
    } else {
      currentMap[attachmentId] = progress
    }
    _transferProgress.value = currentMap
  }

  // --- Draft Management ---

  fun addDraftAttachments(uris: List<Uri>) {
    viewModelScope.launch(Dispatchers.IO) {
      val context = getApplication<Application>()
      val cr = context.contentResolver
      val currentList = _draftAttachments.value.toMutableList()

      for (uri in uris) {
        try {
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
            } catch (_: Exception) {
              null
            }
          } else null

          currentList.add(DraftAttachment(uri, type, mimeTypeRaw, name, preview))
        } catch (e: Exception) {
          Log.e("ConversationVM", "Failed to add draft $uri", e)
        }
      }
      _draftAttachments.value = currentList
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

  private fun resolveUniqueFile(
    dir: File,
    fileName: String,
    contentResolver: ContentResolver,
    sourceUri: Uri
  ): File {
    var file = File(dir, fileName)
    if (!file.exists()) return file

    // Deduplication check: If name exists, check if content is identical
    try {
      val sourceSize =
        contentResolver.openFileDescriptor(sourceUri, "r")?.use { it.statSize } ?: -1L
      if (sourceSize != -1L && file.length() == sourceSize) {
        val sourceHash = calculateStreamHash(contentResolver.openInputStream(sourceUri))
        val targetHash = calculateStreamHash(FileInputStream(file))
        if (sourceHash != null && sourceHash == targetHash) {
          return file // Reuse existing file
        }
      }
    } catch (e: Exception) {
      Log.w("ConversationVM", "Deduplication check failed, falling back to rename", e)
    }

    val name = file.nameWithoutExtension
    val ext = file.extension
    var counter = 1
    while (file.exists()) {
      file = File(dir, "$name($counter).$ext")
      counter++
    }
    return file
  }

  private fun calculateStreamHash(inputStream: java.io.InputStream?): String? {
    inputStream ?: return null
    return try {
      val digest = MessageDigest.getInstance("SHA-256")
      val buffer = ByteArray(8192)
      var bytesRead: Int
      inputStream.use { stream ->
        while (stream.read(buffer).also { bytesRead = it } != -1) {
          digest.update(buffer, 0, bytesRead)
        }
      }
      digest.digest().joinToString("") { "%02x".format(it) }
    } catch (_: Exception) {
      null
    }
  }

  fun updateMessageText(text: String) {
    _messageText.value = text
  }

  // --- Sending ---

  fun sendMessage(messageText: String) {
    val drafts = _draftAttachments.value
    if (messageText.isBlank() && drafts.isEmpty()) return

    if (drafts.isEmpty()) {
      if (nativePtr != 0L) {
        nativeSendMessage(nativePtr, messageText)
      }
      _messageText.value = ""
    } else {
      processAndSend(messageText, drafts)
    }
  }

  private fun processAndSend(text: String, drafts: List<DraftAttachment>) {
    _draftAttachments.value = emptyList()
    _messageText.value = ""

    viewModelScope.launch(Dispatchers.IO) {
      val attachmentRequests = mutableListOf<AttachmentRequest>()
      val context = getApplication<Application>()

      // Use persistent attachments directory
      val attachmentsDir = File(context.filesDir, "attachments")
      if (!attachmentsDir.exists()) attachmentsDir.mkdirs()

      for (draft in drafts) {
        try {
          val destFile =
            resolveUniqueFile(attachmentsDir, draft.name, context.contentResolver, draft.uri)

          // Only copy if we aren't reusing an existing file
          if (!destFile.exists() || destFile.length() == 0L) {
            context.contentResolver.openInputStream(draft.uri)?.use { input ->
              FileOutputStream(destFile).use { output ->
                input.copyTo(output)
              }
            } ?: throw Exception("Failed to open input stream for ${draft.uri}")
          }

          val thumbnailBase64 = try {
            val bitmap = generateThumbnail(context.contentResolver, draft.uri, destFile, draft.type)
            if (bitmap != null) {
              val outputStream = ByteArrayOutputStream()
              bitmap.compress(Bitmap.CompressFormat.JPEG, 60, outputStream)
              val bytes = outputStream.toByteArray()
              Base64.encodeToString(bytes, Base64.NO_WRAP)
            } else ""
          } catch (_: Exception) {
            ""
          }

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

  fun deleteMessage(messageId: Long) {
    if (nativePtr != 0L) nativeDeleteMessage(nativePtr, messageId)
  }

  fun editMessage(messageId: Long, newContent: String) {
    if (nativePtr != 0L) nativeEditMessage(nativePtr, messageId, newContent)
  }

  fun reactToMessage(messageId: Long, reaction: Reaction) {
    if (nativePtr != 0L) nativeReactMessage(nativePtr, messageId, reaction.value)
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

  fun getAttachment(attachmentId: String): Attachment? {
    return if (nativePtr != 0L) nativeGetAttachment(nativePtr, attachmentId) else null
  }

  @Suppress("DEPRECATION")
  private fun generateThumbnail(
    resolver: ContentResolver,
    uri: Uri,
    file: File,
    type: AttachmentType
  ): Bitmap? {
    val size = Size(320, 320)
    return try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        resolver.loadThumbnail(uri, size, CancellationSignal())
      } else {
        if (type == AttachmentType.Video) {
          ThumbnailUtils.createVideoThumbnail(
            file.absolutePath,
            MediaStore.Images.Thumbnails.MINI_KIND
          )
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
    } catch (_: Exception) {
      null
    }
  }

  private fun calculateInSampleSize(
    options: BitmapFactory.Options,
    reqWidth: Int,
    reqHeight: Int
  ): Int {
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
    if (nativePtr != 0L && messageIds.isNotEmpty()) nativeMarkRead(
      nativePtr,
      messageIds.toLongArray()
    )
  }

  fun search(query: String) {
    if (query.isBlank()) {
      _searchResults.value = emptyList()
      return
    }
    viewModelScope.launch {
      val results = withContext(Dispatchers.IO) {
        if (nativePtr != 0L) nativeSearchMessages(nativePtr, peerId, query) else emptyList()
      }
      _searchResults.value = results
    }
  }

  fun loadUpToMessage(messageId: Long) {
    viewModelScope.launch(Dispatchers.IO) {
      if (nativePtr != 0L) nativeLoadUpTo(nativePtr, messageId)
    }
  }

  private external fun nativeCreate(peerId: String): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeSendMessage(ptr: Long, message: String)
  private external fun nativeSendMessageWithAttachments(
    ptr: Long,
    message: String,
    attachments: Array<AttachmentRequest>
  )

  private external fun nativeDeleteMessage(ptr: Long, messageId: Long)
  private external fun nativeEditMessage(ptr: Long, messageId: Long, content: String)
  private external fun nativeReactMessage(ptr: Long, messageId: Long, reaction: Int)

  private external fun nativeLoadOlder(ptr: Long)
  private external fun nativeLoadUpTo(ptr: Long, messageId: Long)
  private external fun nativeLoadFull(ptr: Long, messageId: Long)
  private external fun nativeMarkRead(ptr: Long, messageIds: LongArray)
  private external fun nativeCancelTransfer(ptr: Long, attachmentId: String, isUpload: Boolean)
  private external fun nativeResumeTransfer(ptr: Long, attachmentId: String, isUpload: Boolean)
  private external fun nativeDiscardAttachment(ptr: Long, attachmentId: String)
  private external fun nativeGetAttachment(ptr: Long, attachmentId: String): Attachment?
  private external fun nativeSearchMessages(
    ptr: Long,
    peerId: String,
    query: String
  ): List<HistoryEntry>

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
