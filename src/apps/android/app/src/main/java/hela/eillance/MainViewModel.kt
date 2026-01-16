package hela.eillance

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

class MainViewModel(application: Application) : AndroidViewModel(application) {

  private val _showBetaNotice = MutableStateFlow(false)
  val showBetaNotice = _showBetaNotice.asStateFlow()

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
    checkBetaNotice()
    checkAllPermissions()
  }

  private fun checkBetaNotice() {
    val prefs = getApplication<Application>().getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
    val hasSeenNotice = prefs.getBoolean("has_seen_beta_notice_v1", false)
    _showBetaNotice.value = !hasSeenNotice
  }

  fun dismissBetaNotice() {
    val prefs = getApplication<Application>().getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
    prefs.edit().putBoolean("has_seen_beta_notice_v1", true).apply()
    _showBetaNotice.value = false
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
