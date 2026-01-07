package hela.eillance

import android.app.KeyguardManager
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.IBinder
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
  private var callService: CallService? = null

  private val serviceConnection = object : ServiceConnection {
    override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
      val binder = service as CallService.CallServiceBinder
      callService = binder.getService()
      viewModel.setService(callService)
    }

    override fun onServiceDisconnected(name: ComponentName?) {
      callService = null
      viewModel.setService(null)
    }
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    configureWindowForLockScreen()
    handleIntent(intent)

    // Start service immediately without requesting permissions.
    // Permissions are now handled entirely by on-demand banners in the UI.
    startAndBindService()

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
    // Clear all notifications when the app is opened
    val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    notificationManager.cancelAll()
  }

  override fun onResume() {
    super.onResume()
    // Re-check permissions whenever the activity resumes.
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
      viewModel.onCallAcceptedFromNotification(peerId)
    } else if (isShowIncomingCall && peerId != null) {
      // Logic handled by UI state, but ensure we wake up
      configureWindowForLockScreen()
    } else if (peerId != null) {
      viewModel.navigateToPeer(peerId)
    }
  }

  private fun startAndBindService() {
    Intent(this, CallService::class.java).also { intent ->
      startForegroundService(intent)
      bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE)
    }
  }

  override fun onDestroy() {
    super.onDestroy()
    unbindService(serviceConnection)
  }
}
