package hela.eillance.viewmodels

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import hela.eillance.CallService
import hela.eillance.ndkio.MyProfileInfo
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class ProfileViewModel(application: Application) : AndroidViewModel(application) {
  private var callService: CallService? = null

  private val _myProfile = MutableStateFlow(MyProfileInfo(identityExists = false))
  val myProfile = _myProfile.asStateFlow()

  fun setService(service: CallService?) {
    callService = service
  }

  fun refreshMyProfile() {
    callService?.nativeGetMyProfile()?.let {
      _myProfile.value = it
    }
  }

  fun updateMyProfile(name: String, bio: String, photoBase64: String?) {
    callService?.updateMyProfile(name, bio, photoBase64)
  }

  fun generateNewIdentity(): String {
    return callService?.generateNewIdentity() ?: ""
  }

  fun restoreIdentity(mnemonic: String): Boolean {
    return callService?.restoreIdentity(mnemonic) ?: false
  }

  fun getMnemonic(): String? {
    return callService?.getMnemonic()
  }

  fun restoreBackup(uri: Uri, password: String): Boolean {
    return callService?.restoreBackup(uri, password) ?: false
  }

  fun createBackupToTemp(password: String): String? {
    return callService?.createBackupToTemp(password)
  }

  fun deleteTempBackup(path: String) {
    callService?.deleteTempBackup(path)
  }
}
