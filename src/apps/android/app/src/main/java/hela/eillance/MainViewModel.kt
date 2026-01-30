package hela.eillance

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

class MainViewModel(application: Application) : AndroidViewModel(application) {

  private val prefs = application.getSharedPreferences("mercury_prefs", Context.MODE_PRIVATE)
  private val pm = PermissionManager(application)

  // Real-time permission states
  private val _hasCallPermissions = MutableStateFlow(false)
  val hasCallPermissions = _hasCallPermissions.asStateFlow()

  private val _hasNotificationPermission = MutableStateFlow(true)
  val hasNotificationPermission = _hasNotificationPermission.asStateFlow()

  private val _hasBatteryExemption = MutableStateFlow(true)
  val hasBatteryExemption = _hasBatteryExemption.asStateFlow()

  // "Ignored" states (User clicked "I don't care")
  private val _isBatteryIgnored = MutableStateFlow(prefs.getBoolean(KEY_IGNORE_BATTERY, false))
  private val _isCallPermsIgnored = MutableStateFlow(prefs.getBoolean(KEY_IGNORE_CALLS, false))

  // Navigation states
  private val _incomingCallUiVisible = MutableStateFlow(false)
  val incomingCallUiVisible = _incomingCallUiVisible.asStateFlow()

  private val _navigateToPeerId = MutableStateFlow<String?>(null)
  val navigateToPeerId = _navigateToPeerId.asStateFlow()

  private val _navigateToScanner = MutableStateFlow(false)
  val navigateToScanner = _navigateToScanner.asStateFlow()

  private var isEngineReady = false

  // Computed state for which permission banner to show (if any)
  val permissionBannerState: StateFlow<BannerType?> = combine(
    _hasNotificationPermission,
    _hasBatteryExemption,
    _isBatteryIgnored,
    _hasCallPermissions,
    _isCallPermsIgnored
  ) { hasNotif, hasBat, batIgnored, hasCall, callIgnored ->
    when {
      !hasNotif -> BannerType.Notification
      !hasBat && !batIgnored -> BannerType.Battery
      !hasCall && !callIgnored -> BannerType.CallPermissions
      else -> null
    }
  }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), null)

  // Computed state for the profile "Red Dot" (issues exist, even if ignored)
  val hasPermissionIssues: StateFlow<Boolean> = combine(
    _hasNotificationPermission,
    _hasBatteryExemption,
    _hasCallPermissions
  ) { hasNotif, hasBat, hasCall ->
    !hasNotif || !hasBat || !hasCall
  }.stateIn(viewModelScope, SharingStarted.WhileSubscribed(5000), false)

  init {
    checkAllPermissions()
  }

  fun checkAllPermissions() {
    _hasCallPermissions.value = pm.hasCallPermissions()
    _hasNotificationPermission.value = pm.hasNotificationPermission()
    _hasBatteryExemption.value = pm.hasBatteryExemption()
  }

  fun ignoreBatteryOptimization() {
    prefs.edit().putBoolean(KEY_IGNORE_BATTERY, true).apply()
    _isBatteryIgnored.value = true
  }

  fun ignoreCallPermissions() {
    prefs.edit().putBoolean(KEY_IGNORE_CALLS, true).apply()
    _isCallPermsIgnored.value = true
  }

  fun resetIgnoredPermissions() {
    prefs.edit().remove(KEY_IGNORE_BATTERY).remove(KEY_IGNORE_CALLS).apply()
    _isBatteryIgnored.value = false
    _isCallPermsIgnored.value = false
    checkAllPermissions()
  }

  fun showIncomingCallUi() { _incomingCallUiVisible.value = true }
  fun hideIncomingCallUi() { _incomingCallUiVisible.value = false }
  fun onScanQrClicked() { _navigateToScanner.value = true }
  fun onScannerNavigated() { _navigateToScanner.value = false }
  fun navigateToPeer(peerId: String) { _navigateToPeerId.value = peerId }
  fun onNavigationHandled() { _navigateToPeerId.value = null }
  fun onEngineReady() { isEngineReady = true }
  fun isEngineReady(): Boolean = isEngineReady

  companion object {
    private const val KEY_IGNORE_BATTERY = "ignore_battery_opt"
    private const val KEY_IGNORE_CALLS = "ignore_call_perms"
  }
}

enum class BannerType {
  Notification,
  Battery,
  CallPermissions
}
