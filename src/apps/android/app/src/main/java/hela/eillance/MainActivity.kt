package hela.eillance

import android.app.KeyguardManager
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import hela.eillance.theme.EillanceTheme

class MainActivity : ComponentActivity() {

  private val viewModel: MainViewModel by viewModels()

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    configureWindowForLockScreen()
    handleIntent(intent)

    // Start service immediately.
    startService()

    setContent {
      EillanceTheme {
        Surface(
          modifier = Modifier.fillMaxSize(),
          color = MaterialTheme.colorScheme.background,
        ) {
          MercuryApp(viewModel = viewModel)
        }
      }
    }
  }

  private fun configureWindowForLockScreen() {
    setShowWhenLocked(true)
    setTurnScreenOn(true)
    val keyguardManager = getSystemService(Context.KEYGUARD_SERVICE) as KeyguardManager
    keyguardManager.requestDismissKeyguard(this, null)
  }

  override fun onStart() {
    super.onStart()
    MercuryApplication.isAppInForeground = true
    val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    notificationManager.cancelAll()
  }

  override fun onResume() {
    super.onResume()
    viewModel.checkAllPermissions()
  }

  override fun onStop() {
    super.onStop()
    MercuryApplication.isAppInForeground = false
  }

  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    setIntent(intent)
    handleIntent(intent)
  }

  private fun handleIntent(intent: Intent?) {
    val peerId = intent?.getStringExtra(CallService.EXTRA_PEER_ID)
    val isAcceptAction = intent?.action == CallService.ACTION_ACCEPT_CALL
    val isShowIncomingCall = intent?.action == CallService.ACTION_SHOW_INCOMING_CALL

    if (isAcceptAction && peerId != null) {
      // In new architecture, we might just navigate. Acceptance is handled by CallService/Notification action directly if possible,
      // or we instruct VM here. But MainViewModel doesn't have CallVM access directly.
      // We rely on the UI layer (MercuryApp) to see the state.
      // Actually, if we accepted via notification action, the Service handled it. We just need to navigate to call screen.
      viewModel.navigateToPeer(peerId)
    } else if (isShowIncomingCall && peerId != null) {
      configureWindowForLockScreen()
      viewModel.showIncomingCallUi()
    } else if (peerId != null) {
      viewModel.navigateToPeer(peerId)
    }
  }

  private fun startService() {
    Intent(this, CallService::class.java).also { intent ->
      startForegroundService(intent)
    }
  }
}
