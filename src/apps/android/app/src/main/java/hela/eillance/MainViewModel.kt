package hela.eillance

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class MainViewModel(application: Application) : AndroidViewModel(application) {

  private val _hasCallPermissions = MutableStateFlow(false)
  val hasCallPermissions = _hasCallPermissions.asStateFlow()

  private val _hasNotificationPermission = MutableStateFlow(true)
  val hasNotificationPermission = _hasNotificationPermission.asStateFlow()

  private val _hasBatteryExemption = MutableStateFlow(true)
  val hasBatteryExemption = _hasBatteryExemption.asStateFlow()

  private val _incomingCallUiVisible = MutableStateFlow(false)
  val incomingCallUiVisible = _incomingCallUiVisible.asStateFlow()

  private val _navigateToPeerId = MutableStateFlow<String?>(null)
  val navigateToPeerId = _navigateToPeerId.asStateFlow()

  private val _navigateToScanner = MutableStateFlow(false)
  val navigateToScanner = _navigateToScanner.asStateFlow()

  private var isEngineReady = false

  init {
    checkAllPermissions()
  }

  fun checkAllPermissions() {
    val pm = PermissionManager(getApplication())
    _hasCallPermissions.value = pm.hasCallPermissions()
    _hasNotificationPermission.value = pm.hasNotificationPermission()
    _hasBatteryExemption.value = pm.hasBatteryExemption()
  }

  fun showIncomingCallUi() {
    _incomingCallUiVisible.value = true
  }

  fun hideIncomingCallUi() {
    _incomingCallUiVisible.value = false
  }

  fun onScanQrClicked() {
    _navigateToScanner.value = true
  }

  fun onScannerNavigated() {
    _navigateToScanner.value = false
  }

  fun navigateToPeer(peerId: String) {
    _navigateToPeerId.value = peerId
  }

  fun onNavigationHandled() {
    _navigateToPeerId.value = null
  }

  // Called by CallService when engine is ready to signal the UI can safely instantiate VMs
  fun onEngineReady() {
    isEngineReady = true
  }

  fun isEngineReady(): Boolean {
    return isEngineReady
  }
}
