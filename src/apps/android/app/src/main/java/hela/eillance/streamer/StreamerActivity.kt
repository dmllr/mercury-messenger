package hela.eillance.streamer

import android.Manifest
import android.content.ComponentName
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
import androidx.activity.viewModels
import androidx.appcompat.app.AlertDialog
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Settings
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
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontStyle
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
import hela.eillance.theme.AppIcons
import hela.eillance.theme.EillanceTheme
import hela.eillance.theme.extendedColors

class StreamerActivity : ComponentActivity(), ServiceConnection {

  // Initialize ViewModel using the Activity's ViewModels delegate
  private val viewModel: StreamerViewModel by viewModels {
    StreamerViewModel.Factory(applicationContext)
  }

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
            viewModel = viewModel,
            permissionsGranted = permissionsGranted.value,
            onRequestPermissions = { checkAndRequestPermissions() }
          )
        }
      }
    }

    checkAndRequestPermissions()
  }

  @OptIn(ExperimentalMaterial3Api::class)
  @Composable
  fun StreamerApp(
    viewModel: StreamerViewModel,
    permissionsGranted: Boolean,
    onRequestPermissions: () -> Unit
  ) {
    val navController = rememberNavController()
    val currentBackStackEntry by navController.currentBackStackEntryAsState()
    val showBackButton = currentBackStackEntry?.destination?.route != "operation"
    var isUserMenuExpanded by remember { mutableStateOf(false) }

    val connectionState by viewModel.connectionState.collectAsState()

    Scaffold(
      topBar = {
        TopAppBar(
          title = {
            Column {
              Text(text = stringResource(id = R.string.app_name))
              ConnectionStatusDisplay(connectionState = connectionState)
            }
          },
          navigationIcon = {
            if (showBackButton) {
              IconButton(onClick = { navController.popBackStack() }) {
                Icon(
                  imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                  contentDescription = "Back"
                )
              }
            }
          },
          actions = {
            Box {
              IconButton(onClick = { isUserMenuExpanded = true }) {
                Icon(
                  imageVector = Icons.Default.Settings,
                  contentDescription = "Settings"
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
                  viewModel.startService()
                  isUserMenuExpanded = false
                },
                onServiceStop = {
                  viewModel.stopService()
                  isUserMenuExpanded = false
                },
                onRestartService = {
                  viewModel.restartService()
                  isUserMenuExpanded = false
                },
                connectionState = connectionState
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
            viewModel = viewModel
          )
        }
      }
    }
  }

  @Composable
  private fun ConnectionStatusDisplay(connectionState: ServiceConnectionState) {
    val context = LocalContext.current
    val text = connectionState.getResourceString(context)
    val tint = when (connectionState) {
      ServiceConnectionState.Online -> MaterialTheme.extendedColors.success
      else -> MaterialTheme.extendedColors.warning
    }

    Row(
      verticalAlignment = Alignment.CenterVertically,
      modifier = Modifier.padding(top = 2.dp)
    ) {
      // Online/Offline Indicator Icon
      Icon(
        imageVector = when (connectionState) {
          ServiceConnectionState.Online -> AppIcons.StatusStandby
          ServiceConnectionState.Failure -> AppIcons.Failure
          else -> AppIcons.Status2
        },
        contentDescription = text,
        tint = tint,
        modifier = Modifier.size(14.dp)
      )
      Spacer(Modifier.width(4.dp))
      Text(
        text = text,
        style = MaterialTheme.typography.bodySmall,
        color = tint,
        fontStyle = FontStyle.Italic
      )
    }
  }

  @Composable
  private fun UserMenu(
    expanded: Boolean,
    onDismissRequest: () -> Unit,
    onPreferencesClick: () -> Unit,
    onServiceStart: () -> Unit,
    onServiceStop: () -> Unit,
    onRestartService: () -> Unit,
    connectionState: ServiceConnectionState
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
            imageVector = AppIcons.CapturePreferences,
            contentDescription = stringResource(id = R.string.menu_capture_preferences)
          )
        }
      )

      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_stop_service)) },
        onClick = onServiceStop,
        leadingIcon = {
          Icon(
            imageVector = AppIcons.StopService,
            contentDescription = stringResource(id = R.string.menu_stop_service)
          )
        }
      )

      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_start_service)) },
        enabled = connectionState != ServiceConnectionState.Online,
        onClick = onServiceStart,
        leadingIcon = {
          Icon(
            imageVector = AppIcons.StartService,
            contentDescription = stringResource(id = R.string.menu_start_service)
          )
        }
      )

      DropdownMenuItem(
        text = { Text(stringResource(id = R.string.menu_restart_service)) },
        onClick = onRestartService,
        leadingIcon = {
          Icon(
            imageVector = AppIcons.RestartService,
            contentDescription = stringResource(id = R.string.menu_restart_service)
          )
        }
      )
    }
  }

  @Composable
  private fun NavigationGraph(
    navController: NavHostController,
    viewModel: StreamerViewModel
  ) {
    Box(modifier = Modifier.fillMaxSize()) {
      NavHost(
        navController = navController,
        startDestination = "operation",
        modifier = Modifier
      ) {
        composable("operation") {
          OperationScreen(viewModel)
        }
        composable("preferences") {
          val service = viewModel.getService()
          if (service != null) {
            CapturePreferencesScreen(service)
          } else {
            Box(modifier = Modifier.fillMaxSize()) {
              Text(
                text = stringResource(R.string.internal_error_service_not_available),
                modifier = Modifier.align(Alignment.Center)
              )
            }
          }
        }
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
      .setTitle(getString(R.string.permissions_required_dialog_title))
      .setMessage(getString(R.string.permissions_required_dialog_text))
      .setPositiveButton(getString(R.string.permissions_required_dialog_open_settings)) { _, _ ->
        openSettingsScreen()
      }
      .setNegativeButton(android.R.string.cancel) { dialog, _ ->
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
    // Check if we already have a service reference in the ViewModel
    if (viewModel.getService() != null) {
      viewModel.startService()
    } else {
      startStreamingForegroundService()
    }
  }

  private fun startStreamingForegroundService() {
    val streamerServiceIntent = Intent(applicationContext, StreamerService::class.java)

    Log.i(javaClass.simpleName, "Attempting to start and bind StreamerService.")
    ContextCompat.startForegroundService(applicationContext, streamerServiceIntent)

    val bound = applicationContext.bindService(
      streamerServiceIntent,
      this,
      BIND_AUTO_CREATE
    )
    Log.i(javaClass.simpleName, "Called bindService, result: $bound")
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

    viewModel.updateConnectionState()
  }

  override fun onDestroy() {
    unbindCameraService()
    super.onDestroy()
  }

  private fun unbindCameraService() {
    if (viewModel.getService() != null) {
      try {
        unbindService(this)
        viewModel.setStreamerService(null)
      } catch (_: IllegalArgumentException) {
        Log.e(javaClass.simpleName, "Trying to unbind service that is not registered")
      }
    }
  }

  // ServiceConnection implementation
  override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
    val streamerService = (service as StreamerService.StreamingServiceBinder).service
    viewModel.setStreamerService(streamerService)
  }

  override fun onServiceDisconnected(name: ComponentName?) {
    viewModel.setStreamerService(null)
  }
}
