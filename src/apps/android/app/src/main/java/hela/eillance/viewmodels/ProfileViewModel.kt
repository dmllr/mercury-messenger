package hela.eillance.viewmodels

import android.app.Application
import android.net.Uri
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import hela.eillance.ndkio.MyProfileInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File
import java.io.FileOutputStream

class ProfileViewModel(application: Application) : AndroidViewModel(application), NativeProfileObserver {

  private var nativePtr: Long = 0
  private val _myProfile = MutableStateFlow(MyProfileInfo(identityExists = false))
  val myProfile = _myProfile.asStateFlow()

  init {
    nativePtr = nativeCreate(this)
    if (nativePtr == 0L) {
      Log.e("ProfileViewModel", "Failed to create native ProfileViewModel.")
    }
  }

  override fun onCleared() {
    if (nativePtr != 0L) {
      nativeDestroy(nativePtr)
      nativePtr = 0
    }
    super.onCleared()
  }

  override fun onProfileUpdated(profile: MyProfileInfo) {
    _myProfile.value = profile
  }

  fun updateMyProfile(name: String?, bio: String?, photoBase64: String?) {
    if (nativePtr != 0L) nativeUpdateProfile(nativePtr, name, bio, photoBase64)
  }

  fun generateNewIdentity(): String {
    return if (nativePtr != 0L) nativeGenerateIdentity(nativePtr) else ""
  }

  fun restoreIdentity(mnemonic: String): Boolean {
    return if (nativePtr != 0L) nativeRestoreIdentity(nativePtr, mnemonic) else false
  }

  fun getMnemonic(): String? {
    return if (nativePtr != 0L) nativeGetMnemonic(nativePtr) else null
  }

  fun createBackupToTemp(password: String): String? {
    if (nativePtr == 0L) return null
    return try {
      val tempFile = File.createTempFile("mercury-backup-", ".mcr", getApplication<Application>().cacheDir)
      val success = nativeCreateBackup(nativePtr, tempFile.absolutePath, password)
      if (success) tempFile.absolutePath else {
        tempFile.delete()
        null
      }
    } catch (e: Exception) {
      Log.e("ProfileViewModel", "Error creating backup", e)
      null
    }
  }

  fun deleteTempBackup(path: String) {
    try {
      val file = File(path)
      if (file.exists()) file.delete()
    } catch (e: Exception) {
      Log.e("ProfileViewModel", "Error deleting temp backup", e)
    }
  }

  fun restoreBackup(uri: Uri, password: String): Boolean {
    if (nativePtr == 0L) return false
    return try {
      val inputStream = getApplication<Application>().contentResolver.openInputStream(uri) ?: return false
      val tempFile = File.createTempFile("restore_temp", ".mcr", getApplication<Application>().cacheDir)
      FileOutputStream(tempFile).use { output ->
        inputStream.use { input -> input.copyTo(output) }
      }
      val success = nativeRestoreBackup(nativePtr, tempFile.absolutePath, password)
      tempFile.delete()
      success
    } catch (e: Exception) {
      Log.e("ProfileViewModel", "Error restoring backup", e)
      false
    }
  }

  private external fun nativeCreate(observer: NativeProfileObserver): Long
  private external fun nativeDestroy(ptr: Long)
  private external fun nativeUpdateProfile(ptr: Long, name: String?, bio: String?, photo: String?)
  private external fun nativeGenerateIdentity(ptr: Long): String
  private external fun nativeRestoreIdentity(ptr: Long, mnemonic: String): Boolean
  private external fun nativeGetMnemonic(ptr: Long): String?
  private external fun nativeCreateBackup(ptr: Long, path: String, password: String): Boolean
  private external fun nativeRestoreBackup(ptr: Long, path: String, password: String): Boolean
}
