package hela.eillance.components

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import hela.eillance.BannerType
import hela.eillance.camera.R
import hela.eillance.ndkio.ServiceConnectionState

@Composable
fun GlobalStatusBanner(
  isCoreUnresponsive: Boolean = false,
  connectionState: ServiceConnectionState,
  bannerState: BannerType?,
  onResetCore: () -> Unit = {},
  onCheckPermissions: () -> Unit,
  onIgnoreBattery: () -> Unit = {},
  onIgnoreCalls: () -> Unit = {}
) {
  val context = LocalContext.current
  val notifLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { onCheckPermissions() }
  val callLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { onCheckPermissions() }

  val isConnectionIssue = !isCoreUnresponsive && (
      connectionState != ServiceConnectionState.Online &&
      connectionState != ServiceConnectionState.Establishing &&
      connectionState != ServiceConnectionState.Unknown
  )
  val showPermissionBanner = !isCoreUnresponsive && !isConnectionIssue && bannerState != null

  AnimatedVisibility(
    visible = isCoreUnresponsive || isConnectionIssue || showPermissionBanner,
    enter = expandVertically(expandFrom = Alignment.Top) + fadeIn(),
    exit = shrinkVertically(shrinkTowards = Alignment.Top) + fadeOut()
  ) {
    Box(modifier = Modifier.padding(16.dp)) {
      if (isCoreUnresponsive) {
        StatusSnackbar(
          text = stringResource(R.string.core_error_unresponsive),
          actionLabel = stringResource(R.string.action_reset_core),
          onAction = onResetCore,
          isError = true
        )
      } else if (isConnectionIssue) {
        val (msg, isError) = when (connectionState) {
          ServiceConnectionState.NoNetwork -> stringResource(R.string.connection_no_network) to false
          ServiceConnectionState.Failure -> stringResource(R.string.connection_failed) to true
          ServiceConnectionState.Disconnected -> stringResource(R.string.connection_disconnected) to false
          ServiceConnectionState.InvalidToken -> stringResource(R.string.connection_auth_error) to true
          else -> stringResource(R.string.connection_issue) to true
        }
        StatusSnackbar(text = msg, isError = isError)
      } else {
        when (bannerState) {
          BannerType.Notification -> {
            StatusSnackbar(
              text = stringResource(id = R.string.permission_banner_notifications),
              actionLabel = stringResource(id = R.string.action_fix_it),
              onAction = {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                  notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                } else {
                  openAppSettings(context)
                }
              }
            )
          }
          BannerType.Battery -> {
            StatusSnackbar(
              text = stringResource(id = R.string.permission_banner_battery),
              actionLabel = stringResource(id = R.string.action_fix_it),
              onAction = {
                val intent = @SuppressLint("BatteryLife") Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                  data = "package:${context.packageName}".toUri()
                }
                try {
                  context.startActivity(intent)
                } catch (_: Exception) {
                  openAppSettings(context)
                }
              },
              dismissLabel = stringResource(id = R.string.action_ignore_permission),
              onDismiss = onIgnoreBattery
            )
          }
          BannerType.CallPermissions -> {
            StatusSnackbar(
              text = stringResource(id = R.string.permission_banner_calls),
              actionLabel = stringResource(id = R.string.action_fix_it),
              onAction = {
                callLauncher.launch(
                  arrayOf(
                    Manifest.permission.CAMERA,
                    Manifest.permission.RECORD_AUDIO,
                    Manifest.permission.READ_PHONE_STATE,
                    Manifest.permission.MANAGE_OWN_CALLS
                  )
                )
              },
              dismissLabel = stringResource(id = R.string.action_ignore_permission),
              onDismiss = onIgnoreCalls
            )
          }
          null -> {}
        }
      }
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
