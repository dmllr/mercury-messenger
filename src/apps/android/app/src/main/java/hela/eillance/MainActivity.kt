package hela.eillance

import android.Manifest
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.core.content.ContextCompat
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

  private val requestPermissionLauncher =
    registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
      if (permissions.values.all { it }) {
        startAndBindService()
      } else {
        Toast.makeText(this, "Permissions are required for this app to function.", Toast.LENGTH_LONG).show()
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    handleIntent(intent)
    checkPermissionsAndStart()

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

  override fun onStart() {
    super.onStart()
    MercuryApplication.isAppInForeground = true
    // Clear all notifications when the app is opened
    val notificationManager = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    notificationManager.cancelAll()
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

    if (isAcceptAction && peerId != null) {
      viewModel.onCallAcceptedFromNotification(peerId)
    } else if (peerId != null) {
      // This handles "new message" notifications.
      viewModel.navigateToPeer(peerId)
    }
  }

  private fun checkPermissionsAndStart() {
    val requiredPermissions = mutableListOf(
      Manifest.permission.CAMERA,
      Manifest.permission.RECORD_AUDIO
    )
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      requiredPermissions.add(Manifest.permission.POST_NOTIFICATIONS)
    }

    val allGranted = requiredPermissions.all {
      ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    if (allGranted) {
      startAndBindService()
    } else {
      requestPermissionLauncher.launch(requiredPermissions.toTypedArray())
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
