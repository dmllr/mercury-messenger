package hela.eillance

import android.app.Application
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

data class SharedContent(
  val text: String? = null,
  val uris: List<Uri> = emptyList()
)

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

  private val _navigateToPeerId = MutableStateFlow<Pair<String, Long?>?>(null)
  val navigateToPeerId = _navigateToPeerId.asStateFlow()

  private val _navigateToScanner = MutableStateFlow(false)
  val navigateToScanner = _navigateToScanner.asStateFlow()

  private val _navigateToContacts = MutableStateFlow(false)
  val navigateToContacts = _navigateToContacts.asStateFlow()

  private val _pendingShare = MutableStateFlow<SharedContent?>(null)
  val pendingShare = _pendingShare.asStateFlow()

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

  // Monitor Core Health. Defaults to false if service isn't bound yet.
  // The UI observes this to show the "Reset" banner.
  val isCoreUnresponsive: StateFlow<Boolean> = MessengerService.instance?.isCoreUnresponsive
    ?: MutableStateFlow(false)

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

  fun onContactsNavigated() {
    _navigateToContacts.value = false
  }

  fun navigateToPeer(peerId: String, messageId: Long? = null) {
    _navigateToPeerId.value = peerId to messageId
  }

  fun onNavigationHandled() {
    _navigateToPeerId.value = null
  }

  fun onEngineReady() {
    isEngineReady = true
  }

  fun isEngineReady(): Boolean = isEngineReady

  fun handleShareIntent(intent: Intent) {
    val action = intent.action
    val type = intent.type

    if (Intent.ACTION_SEND == action && type != null) {
      val text = intent.getStringExtra(Intent.EXTRA_TEXT)
      val uri = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
      } else {
        @Suppress("DEPRECATION")
        intent.getParcelableExtra(Intent.EXTRA_STREAM)
      }

      if (text != null || uri != null) {
        _pendingShare.value = SharedContent(
          text = text,
          uris = if (uri != null) listOf(uri) else emptyList()
        )
        _navigateToContacts.value = true
      }
    } else if (Intent.ACTION_SEND_MULTIPLE == action && type != null) {
      val text = intent.getStringExtra(Intent.EXTRA_TEXT)
      val uris = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri::class.java)
      } else {
        @Suppress("DEPRECATION")
        intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM)
      }

      if (!uris.isNullOrEmpty() || text != null) {
        _pendingShare.value = SharedContent(
          text = text,
          uris = uris ?: emptyList()
        )
        _navigateToContacts.value = true
      }
    }
  }

  fun consumePendingShareContent(): SharedContent? {
    val content = _pendingShare.value
    _pendingShare.value = null
    return content
  }

  fun resetCore() {
    MessengerService.instance?.restartCore()
  }

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
