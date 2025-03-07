package hela.eillance.streamer

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.provider.Settings
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AlertDialog
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import hela.eillance.camera.R
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.theme.EillanceTheme

class StreamerActivity : ComponentActivity(), ServiceConnection {

  private var streamerService: StreamerService? = null
  private var permissionsGranted = mutableStateOf(false)

  private val requiredPermissions = mutableListOf(
    Manifest.permission.CAMERA,
    Manifest.permission.RECORD_AUDIO
  ).apply {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      add(Manifest.permission.POST_NOTIFICATIONS)
    }
  }

  private val permissionLauncher = registerForActivityResult(
    ActivityResultContracts.RequestMultiplePermissions()
  ) { permissions ->
    if (permissions.all { it.value }) {
      permissionsGranted.value = true
      startStreamingService()
    } else {
      showSettingsRedirectDialog()
    }
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    // Initialize permissions state
    permissionsGranted.value = arePermissionsGranted()

    setContent {
      EillanceTheme {
        Surface(
          modifier = Modifier.fillMaxSize(),
          color = MaterialTheme.colorScheme.background
        ) {
          StreamerApp(
            permissionsGranted = permissionsGranted.value,
            onRequestPermissions = { checkAndRequestPermissions() },
            streamerService = streamerService,
            onStartService = { startStreamingService() },
            onStopService = { stopStreamingService() },
            onRestartService = { restartStreamingService() }
          )
        }
      }
    }

    checkAndRequestPermissions()
  }

  @OptIn(ExperimentalMaterial3Api::class)
  @Composable
  fun StreamerApp(
    permissionsGranted: Boolean,
    onRequestPermissions: () -> Unit,
    streamerService: StreamerService?,
    onStartService: () -> Unit,
    onStopService: () -> Unit,
    onRestartService: () -> Unit
  ) {
    val navController = rememberNavController()
    val currentBackStackEntry by navController.currentBackStackEntryAsState()
    val showBackButton = currentBackStackEntry?.destination?.route != "operation"
    var isUserMenuExpanded by remember { mutableStateOf(false) }

    Scaffold(
      topBar = {
        TopAppBar(
          title = { Text(text = stringResource(id = R.string.app_name)) },
          navigationIcon = {
            if (showBackButton) {
              IconButton(onClick = { navController.popBackStack() }) {
                Icon(
                  painter = painterResource(id = R.drawable.ic_status2),
                  contentDescription = stringResource(id = android.R.string.cancel)
                )
              }
            }
          },
          actions = {
            Box {
              IconButton(onClick = { isUserMenuExpanded = true }) {
                Icon(
                  painter = painterResource(id = R.drawable.ic_status3),
                  contentDescription = stringResource(id = R.string.toolbar_user_icon)
                )
              }
              UserMenu(
                expanded = isUserMenuExpanded,
                onDismissRequest = { isUserMenuExpanded = false },
                onPreferencesClick = {
                  navController.navigate("preferences")
                  isUserMenuExpanded = false
                },
                onServiceStart = {
                  onStartService()
                  isUserMenuExpanded = false
                },
                onServiceStop = {
                  onStopService()
                  isUserMenuExpanded = false
                },
                onRestartService = {
                  onRestartService()
                  isUserMenuExpanded = false
                }
              )
            }
          }
        )
      }
    ) { paddingValues ->
      Box(modifier = Modifier.padding(paddingValues)) {
        if (!permissionsGranted) {
          PermissionsScreen(onRequestPermissions = onRequestPermissions)
        } else {
          NavigationGraph(
            navController = navController,
            streamerService = streamerService
          )
        }
      }
    }
  }

  @Composable
  private fun UserMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    onPreferencesClick: () -> Unit,
    onServiceStart: () -> Unit,
    onServiceStop: () -> Unit,
    onRestartService: () -> Unit
  ) {
    DropdownMenu(
      expanded = expanded,
      onDismissRequest = onDismissRequest
    ) {
      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_capture_preferences)) },
        onClick = onPreferencesClick,
        leadingIcon = {
          Icon(
            painter = painterResource(id = R.drawable.ic_capture_preferences),
            contentDescription = stringResource(id = R.string.menu_capture_preferences)
          )
        }
      )

      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_stop_service)) },
        onClick = onServiceStop,
        leadingIcon = {
          Icon(
            painter = painterResource(id = R.drawable.ic_stop_service),
            contentDescription = stringResource(id = R.string.menu_stop_service)
          )
        }
      )

      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_start_service)) },
        enabled = streamerService == null
            || streamerService!!.getServiceConnectionState() != ServiceConnectionState.Online,
        onClick = onServiceStart,
        leadingIcon = {
          Icon(
            painter = painterResource(id = R.drawable.ic_start_service),
            contentDescription = stringResource(id = R.string.menu_start_service)
          )
        }
      )

      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_restart_service)) },
        onClick = onRestartService,
        leadingIcon = {
          Icon(
            painter = painterResource(id = R.drawable.ic_restart_service),
            contentDescription = stringResource(id = R.string.menu_restart_service)
          )
        }
      )
    }
  }

  @Composable
  private fun NavigationGraph(
    navController: NavHostController,
    streamerService: StreamerService?
  ) {
    NavHost(
      navController = navController,
      startDestination = "operation"
    ) {
      composable("operation") {
        OperationScreen(streamerService = streamerService)
      }
      composable("preferences") {
        CapturePreferencesScreen(streamerService = streamerService!!)
      }
    }
  }

  @Composable
  private fun PermissionsScreen(onRequestPermissions: () -> Unit) {
    Box(
      modifier = Modifier.fillMaxSize(),
      contentAlignment = Alignment.TopCenter
    ) {
      Button(
        onClick = onRequestPermissions,
        modifier = Modifier.padding(16.dp),
        shape = MaterialTheme.shapes.extraSmall,
        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
      ) {
        Text(
          text = stringResource(id = R.string.permissions_required),
          modifier = Modifier.padding(8.dp),
        )
      }
    }
  }

  private fun arePermissionsGranted(): Boolean {
    return requiredPermissions.all {
      ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }
  }

  private fun checkAndRequestPermissions() {
    if (!arePermissionsGranted()) {
      val shouldShowRationale = requiredPermissions.any {
        shouldShowRequestPermissionRationale(it)
      }

      if (shouldShowRationale) {
        showPermissionRationale(requiredPermissions.toTypedArray())
      } else {
        permissionLauncher.launch(requiredPermissions.toTypedArray())
      }
    } else {
      // Update permissions state when all permissions are granted
      permissionsGranted.value = true
      startStreamingService()
    }
  }

  private fun showPermissionRationale(permissions: Array<String>) {
    AlertDialog.Builder(this)
      .setTitle(getString(R.string.permissions_required_rationale_title))
      .setMessage(getString(R.string.permissions_required_rationale_text))
      .setPositiveButton(getString(R.string.grant_permissions)) { _, _ ->
        permissionLauncher.launch(permissions)
      }
      .setNegativeButton(getString(android.R.string.cancel)) { dialog, _ ->
        dialog.dismiss()
      }
      .setCancelable(false)
      .show()
  }

  private fun showSettingsRedirectDialog() {
    AlertDialog.Builder(this)
      .setTitle("Permissions Required")
      .setMessage("You have denied necessary permissions. To enable streaming, please allow them in settings.")
      .setPositiveButton("Open Settings") { _, _ ->
        openSettingsScreen()
      }
      .setNegativeButton("Cancel") { dialog, _ ->
        dialog.dismiss()
      }
      .show()
  }

  private fun openSettingsScreen() {
    val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
      data = "package:$packageName".toUri()
    }
    startActivity(intent)
  }

  private fun startStreamingService() {
    if (streamerService != null) {
      streamerService?.start()
    } else {
      startStreamingForegroundService()
    }
  }

  private fun stopStreamingService() {
    streamerService?.stop()
  }

  private fun restartStreamingService() {
    streamerService?.let { service ->
      service.stop()
      service.start()
    }
  }

  private fun startStreamingForegroundService() {
    val streamerServiceIntent = Intent(applicationContext, StreamerService::class.java)
    startService(streamerServiceIntent)
    applicationContext.bindService(
      streamerServiceIntent,
      this,
      Context.BIND_AUTO_CREATE + Context.BIND_ABOVE_CLIENT
    )
  }

  override fun onResume() {
    super.onResume()
    // Check permissions again when returning to the app
    val currentPermissionsStatus = arePermissionsGranted()
    if (currentPermissionsStatus != permissionsGranted.value) {
      permissionsGranted.value = currentPermissionsStatus
      if (currentPermissionsStatus) {
        startStreamingService()
      }
    }
  }

  override fun onDestroy() {
    unbindCameraService()
    super.onDestroy()
  }

  private fun unbindCameraService() {
    if (streamerService != null) {
      try {
        unbindService(this)
      } catch (_: IllegalArgumentException) {
        Log.e(javaClass.simpleName, "Trying to unbind service that is not registered")
      }
      streamerService = null
    }
  }

  // ServiceConnection implementation
  override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
    streamerService = (service as StreamerService.StreamingServiceBinder).service
  }

  override fun onServiceDisconnected(name: ComponentName?) {
    streamerService = null
  }
}