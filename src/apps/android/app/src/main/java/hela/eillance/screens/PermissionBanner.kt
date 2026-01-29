package hela.eillance.screens

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BatteryAlert
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.PermCameraMic
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.app.ActivityCompat
import androidx.core.net.toUri

@Composable
fun CallPermissionBanner(
  onCheckPermissions: () -> Unit
) {
  val context = LocalContext.current
  var showSettingsRedirect by remember { mutableStateOf(false) }

  val permissions = remember {
    val list = mutableListOf(
      Manifest.permission.CAMERA,
      Manifest.permission.RECORD_AUDIO,
      Manifest.permission.READ_PHONE_STATE // Dangerous permission, triggers "Phone" dialog
    )
    list.add(Manifest.permission.MANAGE_OWN_CALLS)
    list.toTypedArray()
  }

  val launcher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.RequestMultiplePermissions()
  ) { results ->
    // Check for permanent denial (Don't ask again)
    // If any permission is NOT granted AND shouldShowRequestPermissionRationale is false,
    // it means the user has permanently denied it (or selected "Don't ask again").
    val anyPermanentlyDenied = results.any { (permission, granted) ->
      !granted && !ActivityCompat.shouldShowRequestPermissionRationale(context as Activity, permission)
    }

    if (anyPermanentlyDenied) {
      showSettingsRedirect = true
    }

    // Always refresh state in ViewModel
    onCheckPermissions()
  }

  Surface(
    color = MaterialTheme.colorScheme.tertiaryContainer,
    modifier = Modifier
      .fillMaxWidth()
      .clickable {
        if (showSettingsRedirect) {
          openAppSettings(context)
        } else {
          launcher.launch(permissions)
        }
      }
  ) {
    Row(
      modifier = Modifier
        .fillMaxWidth()
        .padding(16.dp),
      verticalAlignment = Alignment.CenterVertically,
      horizontalArrangement = Arrangement.Center
    ) {
      Icon(
        imageVector = if (showSettingsRedirect) Icons.Default.Settings else Icons.Default.PermCameraMic,
        contentDescription = null,
        tint = MaterialTheme.colorScheme.onTertiaryContainer
      )
      Spacer(modifier = Modifier.width(16.dp))
      Text(
        text = if (showSettingsRedirect) "Permissions blocked. Tap to open Settings." else "Tap to enable calls (Camera, Mic, Phone)",
        color = MaterialTheme.colorScheme.onTertiaryContainer,
        style = MaterialTheme.typography.bodyMedium
      )
    }
  }
}

@Composable
fun NotificationPermissionBanner(
  onCheckPermissions: () -> Unit
) {
  if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) return

  val context = LocalContext.current
  var showSettingsRedirect by remember { mutableStateOf(false) }

  val launcher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.RequestPermission()
  ) { isGranted ->
    if (!isGranted) {
      val showRationale = ActivityCompat.shouldShowRequestPermissionRationale(context as Activity, Manifest.permission.POST_NOTIFICATIONS)
      // If denied and we shouldn't show rationale, it's permanently denied
      if (!showRationale) {
        showSettingsRedirect = true
      }
    }
    onCheckPermissions()
  }

  Surface(
    color = MaterialTheme.colorScheme.secondaryContainer,
    modifier = Modifier
      .fillMaxWidth()
      .clickable {
        if (showSettingsRedirect) {
          openAppSettings(context)
        } else {
          launcher.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
      }
  ) {
    Row(
      modifier = Modifier
        .fillMaxWidth()
        .padding(16.dp),
      verticalAlignment = Alignment.CenterVertically,
      horizontalArrangement = Arrangement.Center
    ) {
      Icon(
        imageVector = if (showSettingsRedirect) Icons.Default.Settings else Icons.Default.Notifications,
        contentDescription = null,
        tint = MaterialTheme.colorScheme.onSecondaryContainer
      )
      Spacer(modifier = Modifier.width(16.dp))
      Text(
        text = if (showSettingsRedirect) "Notifications blocked. Tap to Settings." else "Tap to enable notifications",
        color = MaterialTheme.colorScheme.onSecondaryContainer,
        style = MaterialTheme.typography.bodyMedium
      )
    }
  }
}

@SuppressLint("BatteryLife")
@Composable
fun BatteryOptimizationBanner(
  onCheckPermissions: () -> Unit
) {
  val context = LocalContext.current

  Surface(
    color = MaterialTheme.colorScheme.errorContainer,
    modifier = Modifier
      .fillMaxWidth()
      .clickable {
        val intent = Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
          data = "package:${context.packageName}".toUri()
        }
        try {
          context.startActivity(intent)
        } catch (e: Exception) {
          openAppSettings(context)
        }
        onCheckPermissions()
      }
  ) {
    Row(
      modifier = Modifier
        .fillMaxWidth()
        .padding(16.dp),
      verticalAlignment = Alignment.CenterVertically,
      horizontalArrangement = Arrangement.Center
    ) {
      Icon(
        imageVector = Icons.Default.BatteryAlert,
        contentDescription = null,
        tint = MaterialTheme.colorScheme.onErrorContainer
      )
      Spacer(modifier = Modifier.width(16.dp))
      Text(
        text = "Tap to disable battery optimization for reliable calls",
        color = MaterialTheme.colorScheme.onErrorContainer,
        style = MaterialTheme.typography.bodyMedium
      )
    }
  }
}

private fun openAppSettings(context: Context) {
  val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
    data = Uri.fromParts("package", context.packageName, null)
    flags = Intent.FLAG_ACTIVITY_NEW_TASK
  }
  context.startActivity(intent)
}
