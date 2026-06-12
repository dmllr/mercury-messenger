package hela.eillance

import android.app.Application
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import hela.eillance.events.NativeEventBus
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

data class SharedContent(
  val text: String? = null,
  val uris: List<Uri> = emptyList()
)

class MainViewModel(application: Application) : AndroidViewModel(application) {

  private val pm = PermissionManager(application)

  // Real-time permission states
  private val _hasCallPermissions = MutableStateFlow(false)
  val hasCallPermissions = _hasCallPermissions.asStateFlow()

  private val _hasNotificationPermission = MutableStateFlow(true)
  val hasNotificationPermission = _hasNotificationPermission.asStateFlow()

  private val _hasBatteryExemption = MutableStateFlow(true)
  val hasBatteryExemption = _hasBatteryExemption.asStateFlow()

  private val prefs = application.getSharedPreferences("mercury_global_prefs", Context.MODE_PRIVATE)
  private val PREF_MIGRATION_HANDLED = "v200_migration_handled"
  private val PREF_IGNORE_BATTERY = "ignore_battery"
  private val PREF_IGNORE_CALLS = "ignore_calls"

  private val _showMigrationDialog = MutableStateFlow(false)
  val showMigrationDialog = _showMigrationDialog.asStateFlow()

  private val _isBatteryIgnored = MutableStateFlow(prefs.getBoolean(PREF_IGNORE_BATTERY, false))
  private val _isCallsIgnored = MutableStateFlow(prefs.getBoolean(PREF_IGNORE_CALLS, false))

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
    _hasCallPermissions,
    _isBatteryIgnored,
    _isCallsIgnored
  ) { hasNotif, hasBat, hasCall, batIgnored, callsIgnored ->
    when {
      !hasNotif -> BannerType.Notification
      !hasBat && !batIgnored -> BannerType.Battery
      !hasCall && !callsIgnored -> BannerType.CallPermissions
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
    viewModelScope.launch {
      MessengerService.isEngineReady.collect { isReady ->
        if (isReady) {
          checkAllPermissions()
        }
      }
    }
  }

  fun checkAllPermissions() {
    val call = pm.hasCallPermissions()
    val notif = pm.hasNotificationPermission()
    val bat = pm.hasBatteryExemption()
    _hasCallPermissions.value = call
    _hasNotificationPermission.value = notif
    _hasBatteryExemption.value = bat
    MessengerService.instance?.nativeUpdatePlatformPermissions(notif, bat, call)
  }

  fun ignoreBatteryOptimization() {
    prefs.edit().putBoolean(PREF_IGNORE_BATTERY, true).apply()
    _isBatteryIgnored.value = true
    MessengerService.instance?.nativeIgnoreBattery()
  }

  fun ignoreCallPermissions() {
    prefs.edit().putBoolean(PREF_IGNORE_CALLS, true).apply()
    _isCallsIgnored.value = true
    MessengerService.instance?.nativeIgnoreCalls()
  }

  fun resetIgnoredPermissions() {
    prefs.edit().putBoolean(PREF_IGNORE_BATTERY, false).putBoolean(PREF_IGNORE_CALLS, false).apply()
    _isBatteryIgnored.value = false
    _isCallsIgnored.value = false
    MessengerService.instance?.nativeResetIgnoredPermissions()
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

  fun evaluateMigrationCondition(contactCount: Int) {
    if (!prefs.getBoolean(PREF_MIGRATION_HANDLED, false)) {
      if (contactCount > 0) {
        _showMigrationDialog.value = true
      } else {
        markMigrationHandled()
      }
    }
  }

  fun markMigrationHandled() {
    prefs.edit().putBoolean(PREF_MIGRATION_HANDLED, true).apply()
    _showMigrationDialog.value = false
  }

  fun onEngineReady() {
    isEngineReady = true
  }

  fun setPendingShare(content: SharedContent) {
    _pendingShare.value = content
    _navigateToContacts.value = true
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

  fun clearPendingShare() {
    _pendingShare.value = null
  }

  fun resetCore() {
    MessengerService.instance?.restartCore()
  }
}

enum class BannerType {
  Notification,
  Battery,
  CallPermissions
}
