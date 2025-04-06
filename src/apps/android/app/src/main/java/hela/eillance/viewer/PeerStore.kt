package hela.eillance.viewer

import android.content.Context
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.util.Log
import androidx.core.content.edit
import hela.eillance.ndkio.Profile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File
import java.io.FileOutputStream

@Serializable
data class StoredPeer(
  val id: String,
  var name: String,
  var profile: Profile,
  var previewImagePath: String? = null,
  var isOnline: Boolean = false,
  var lastSelectedCameraIndex: Int = 0,
  var lastSelectedResolutionIndex: Int = 0
)

// Class to manage storing and retrieving peer data
class PeerStore(
  context: Context,
  private val externalScope: CoroutineScope
) {
  private val prefs: SharedPreferences = context.getSharedPreferences("peer_store", Context.MODE_PRIVATE)
  private val json = Json {
    ignoreUnknownKeys = true
    prettyPrint = false
  }
  private val previewDir = File(context.filesDir, "peer_previews")

  internal val _storedPeers = MutableStateFlow<Map<String, StoredPeer>>(emptyMap())
  val storedPeers: StateFlow<Map<String, StoredPeer>> = _storedPeers.asStateFlow()

  companion object {
    private const val PEERS_KEY = "stored_peers_map"
    private const val TAG = "PeerStore"
    private const val MAX_PREVIEW_WIDTH = 800
    private const val MAX_PREVIEW_HEIGHT = 600
    private const val MAX_LOADED_WIDTH = 1200
    private const val MAX_LOADED_HEIGHT = 900
  }

  init {
    if (!previewDir.exists()) {
      previewDir.mkdirs()
    }
    loadPeers()
  }

  private fun loadPeers() {
    externalScope.launch(Dispatchers.IO) {
      val jsonString = prefs.getString(PEERS_KEY, null)
      val loadedMap = if (jsonString != null) {
        try {
          json.decodeFromString<Map<String, StoredPeer>>(jsonString)
        } catch (e: Exception) {
          Log.e(TAG, "Failed to decode stored peers", e)
          prefs.edit {
            remove(PEERS_KEY)  // clear corrupted
          }
          emptyMap()
        }
      } else {
        emptyMap()
      }

      // ensure all initially loaded peers are marked offline
      val offlineMap = loadedMap.mapValues { it.value.copy(isOnline = false) }
      _storedPeers.value = offlineMap // Update the flow on the main thread implicitly via StateFlow
      Log.d(TAG, "Loaded ${offlineMap.size} peers from store.")
    }
  }

  internal fun savePeers() {
    externalScope.launch(Dispatchers.IO) {
      try {
        val currentPeers = _storedPeers.value
        val jsonString = json.encodeToString(currentPeers)
        prefs.edit {
          putString(PEERS_KEY, jsonString)
        }
        Log.d(TAG, "Saved ${currentPeers.size} peers to store.")
      } catch (e: Exception) {
        Log.e(TAG, "Failed to encode or save peers", e)
      }
    }
  }

  suspend fun updatePeerStatus(peerId: String, isOnline: Boolean) = withContext(Dispatchers.IO) {
    var statusChanged = false
    _storedPeers.update { currentMap ->
      val peer = currentMap[peerId]
      if (peer != null && peer.isOnline != isOnline) {
        statusChanged = true
        currentMap + (peerId to peer.copy(isOnline = isOnline))
      } else {
        currentMap // no change needed
      }
    }
    if (statusChanged) {
      Log.d(TAG, "Updated status for peer: $peerId, Online: $isOnline")
      savePeers()
    }
  }

  suspend fun updateAllPeersOffline() = withContext(Dispatchers.IO) {
    var changed = false
    _storedPeers.update { currentMap ->
      val offlineMap = currentMap.mapValues { entry ->
        if (entry.value.isOnline) {
          changed = true
          entry.value.copy(isOnline = false)
        } else {
          entry.value
        }
      }
      if (changed) {
        Log.d(TAG, "Set all stored peers to offline.")
        savePeers()
      }
      offlineMap
    }
  }

  /**
   * Resizes a bitmap to fit within the specified dimensions while maintaining aspect ratio.
   */
  private fun resizeBitmap(bitmap: Bitmap, maxWidth: Int, maxHeight: Int): Bitmap {
    val width = bitmap.width
    val height = bitmap.height

    // If bitmap is already smaller than max dimensions, return original
    if (width <= maxWidth && height <= maxHeight) {
      return bitmap
    }

    // Calculate the scaling factor
    val scaleFactor = (maxWidth.toFloat() / width).coerceAtMost(maxHeight.toFloat() / height)

    // Create a matrix for the scaling
    val matrix = Matrix()
    matrix.postScale(scaleFactor, scaleFactor)

    // Recreate the new bitmap
    val resizedBitmap = Bitmap.createBitmap(
      bitmap, 0, 0,
      width, height, matrix, true
    )

    // If we created a new bitmap, recycle the original one to free memory
    if (bitmap != resizedBitmap) {
      bitmap.recycle()
    }

    Log.d(TAG, "Resized bitmap from ${width}x${height} to ${resizedBitmap.width}x${resizedBitmap.height}")
    return resizedBitmap
  }

  suspend fun savePreviewImage(peerId: String, bitmap: Bitmap): String? = withContext(Dispatchers.IO) {
    val file = File(previewDir, "$peerId.png")
    var success = false

    try {
      // Resize the bitmap to save storage space and memory
      val resizedBitmap = resizeBitmap(bitmap, MAX_PREVIEW_WIDTH, MAX_PREVIEW_HEIGHT)

      FileOutputStream(file).use { fos ->
        resizedBitmap.compress(Bitmap.CompressFormat.PNG, 90, fos)
        success = true
      }

      // If resizedBitmap is different from input bitmap, recycle it
      if (resizedBitmap != bitmap) {
        resizedBitmap.recycle()
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to save preview image for $peerId", e)
    }

    if (success) {
      val path = file.absolutePath
      // update the stored peer data with the new path
      _storedPeers.update { currentMap ->
        val peer = currentMap[peerId]
        if (peer != null && peer.previewImagePath != path) {
          val newMap = currentMap + (peerId to peer.copy(previewImagePath = path))
          savePeers()
          newMap
        } else {
          currentMap
        }
      }
      Log.d(TAG, "Saved preview for peer: $peerId at path: $path")
      path
    } else {
      null
    }
  }

  suspend fun loadPreviewBitmap(imagePath: String?): Bitmap? = withContext(Dispatchers.IO) {
    if (imagePath == null) return@withContext null
    try {
      // First, decode just the bounds to check the size
      val options = BitmapFactory.Options().apply {
        inJustDecodeBounds = true
      }
      BitmapFactory.decodeFile(imagePath, options)

      // Calculate the sample size to use
      var sampleSize = 1
      if (options.outWidth > MAX_LOADED_WIDTH || options.outHeight > MAX_LOADED_HEIGHT) {
        val widthRatio = Math.ceil(options.outWidth.toDouble() / MAX_LOADED_WIDTH).toInt()
        val heightRatio = Math.ceil(options.outHeight.toDouble() / MAX_LOADED_HEIGHT).toInt()
        sampleSize = Math.max(widthRatio, heightRatio)
        Log.d(TAG, "Loading image at sample size $sampleSize (from ${options.outWidth}x${options.outHeight})")
      }

      // Decode with the sample size
      val decodeOptions = BitmapFactory.Options().apply {
        inSampleSize = sampleSize
      }
      val bitmap = BitmapFactory.decodeFile(imagePath, decodeOptions)

      // If needed, do additional resizing to get exact dimensions
      if (bitmap != null && (bitmap.width > MAX_LOADED_WIDTH || bitmap.height > MAX_LOADED_HEIGHT)) {
        return@withContext resizeBitmap(bitmap, MAX_LOADED_WIDTH, MAX_LOADED_HEIGHT)
      }

      bitmap
    } catch (e: Exception) {
      Log.e(TAG, "Failed to load preview image from $imagePath", e)
      null
    }
  }

  private suspend fun deletePreviewImage(imagePath: String?) = withContext(Dispatchers.IO) {
    if (imagePath == null) return@withContext
    try {
      val file = File(imagePath)
      if (file.exists()) {
        file.delete()
        Log.d(TAG, "Deleted preview image: $imagePath")
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to delete preview image $imagePath", e)
    }
  }

  suspend fun removePeer(peerId: String) = withContext(Dispatchers.IO) {
    var imagePathToDelete: String? = null
    _storedPeers.update { currentMap ->
      imagePathToDelete = currentMap[peerId]?.previewImagePath
      val newMap = currentMap - peerId
      savePeers()
      newMap
    }
    deletePreviewImage(imagePathToDelete)
    Log.d(TAG, "Removed peer: $peerId")
  }
}
