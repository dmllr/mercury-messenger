package hela.eillance.screens

import android.Manifest
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.BatteryAlert
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.PermCameraMic
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.core.net.toUri
import hela.eillance.MainViewModel
import hela.eillance.camera.R
import hela.eillance.theme.extendedColors

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PermissionIssuesScreen(
  mainViewModel: MainViewModel,
  onBack: () -> Unit
) {
  val context = LocalContext.current
  val hasNotif by mainViewModel.hasNotificationPermission.collectAsState()
  val hasBattery by mainViewModel.hasBatteryExemption.collectAsState()
  val hasCalls by mainViewModel.hasCallPermissions.collectAsState()

  // Launchers
  val notifLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
      mainViewModel.checkAllPermissions()
    }
  val callLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
      mainViewModel.checkAllPermissions()
    }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_permission_issues)) },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(
              Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = stringResource(id = R.string.action_back)
            )
          }
        }
      )
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
    ) {
      PermissionItem(
        title = stringResource(id = R.string.permission_notifications_title),
        description = stringResource(id = R.string.permission_notifications_description),
        isGranted = hasNotif,
        icon = Icons.Default.Notifications,
        onFix = {
          if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
          } else {
            openAppSettings(context)
          }
        }
      )
      HorizontalDivider()
      PermissionItem(
        title = stringResource(id = R.string.permission_battery_title),
        description = stringResource(id = R.string.permission_battery_description),
        isGranted = hasBattery,
        icon = Icons.Default.BatteryAlert,
        onFix = {
          val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
            data = "package:${context.packageName}".toUri()
          }
          try {
            context.startActivity(intent)
          } catch (e: Exception) {
            openAppSettings(context)
          }
        }
      )
      HorizontalDivider()
      PermissionItem(
        title = stringResource(id = R.string.permission_calls_title),
        description = stringResource(id = R.string.permission_calls_description),
        isGranted = hasCalls,
        icon = Icons.Default.PermCameraMic,
        onFix = {
          callLauncher.launch(
            arrayOf(
              Manifest.permission.CAMERA,
              Manifest.permission.RECORD_AUDIO,
              Manifest.permission.READ_PHONE_STATE,
              Manifest.permission.MANAGE_OWN_CALLS
            )
          )
        }
      )

      HorizontalDivider()

      // Reset ignored preferences
      ListItem(
        headlineContent = { Text(stringResource(id = R.string.permission_reset_ignored_title)) },
        supportingContent = { Text(stringResource(id = R.string.permission_reset_ignored_description)) },
        trailingContent = {
          Button(onClick = { mainViewModel.resetIgnoredPermissions() }) {
            Text(stringResource(id = R.string.action_reset))
          }
        }
      )
    }
  }
}

@Composable
private fun PermissionItem(
  title: String,
  description: String,
  isGranted: Boolean,
  icon: androidx.compose.ui.graphics.vector.ImageVector,
  onFix: () -> Unit
) {
  ListItem(
    headlineContent = { Text(title) },
    supportingContent = { Text(description) },
    leadingContent = {
      if (isGranted) {
        Icon(
          Icons.Default.CheckCircle,
          contentDescription = null,
          tint = MaterialTheme.extendedColors.success
        )
      } else {
        Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.error)
      }
    },
    trailingContent = {
      if (!isGranted) {
        Button(onClick = onFix) { Text(stringResource(id = R.string.action_fix_it)) }
      }
    }
  )
}

private fun openAppSettings(context: Context) {
  val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
    data = Uri.fromParts("package", context.packageName, null)
    flags = Intent.FLAG_ACTIVITY_NEW_TASK
  }
  context.startActivity(intent)
}
