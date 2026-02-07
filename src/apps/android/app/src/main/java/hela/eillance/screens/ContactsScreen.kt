package hela.eillance.screens

import android.Manifest
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
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DividerDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.core.os.ConfigurationCompat
import hela.eillance.BannerType
import hela.eillance.MainViewModel
import hela.eillance.camera.R
import hela.eillance.components.ContactListItem
import hela.eillance.components.StatusSnackbar
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ContactStatus
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.viewmodels.ContactsViewModel
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
  contactsVM: ContactsViewModel,
  mainViewModel: MainViewModel,
  myProfile: MyProfileInfo,
  contacts: List<Contact>,
  isLoading: Boolean,
  connectionState: ServiceConnectionState,
  onCheckPermissions: () -> Unit,
  onContactClick: (String) -> Unit,
  onNavigateToProfile: () -> Unit,
  onShowContactProfile: (String) -> Unit
) {
  val snackbarHostState = remember { SnackbarHostState() }
  val context = LocalContext.current
  val bannerState by mainViewModel.permissionBannerState.collectAsState()
  val hasPermissionIssues by mainViewModel.hasPermissionIssues.collectAsState()
  val pendingShare by mainViewModel.pendingShare.collectAsState()

  val notifLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { onCheckPermissions() }
  val callLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { onCheckPermissions() }

  LaunchedEffect(contactsVM) {
    contactsVM.errorMessage.collect { message ->
      snackbarHostState.showSnackbar(message)
    }
  }

  Scaffold(
    snackbarHost = { SnackbarHost(snackbarHostState) },
    topBar = {
      TopAppBar(
        title = {
          Row(verticalAlignment = Alignment.CenterVertically) {
            Box {
              IconButton(onClick = onNavigateToProfile) {
                Box {
                  UserAvatar(
                    photoBase64 = myProfile.myPhoto,
                    modifier = Modifier.size(32.dp)
                  )
                }
              }

              if (hasPermissionIssues) {
                Box(
                  modifier = Modifier
                    .align(Alignment.TopEnd)
                    .padding(top = 8.dp, end = 8.dp)
                    .size(10.dp)
                    .background(MaterialTheme.colorScheme.error, CircleShape)
                    .border(1.dp, MaterialTheme.colorScheme.surface, CircleShape)
                )
              }
            }

            Spacer(modifier = Modifier.width(8.dp))

            Text(
              text = if (pendingShare != null) {
                stringResource(id = R.string.share_with)
              } else {
                "\uD835\uDCDC\uD835\uDCEE\uD835\uDCFB\uD835\uDCEC\uD835\uDCFE\uD835\uDCFB\uD835\uDD02"
              },
              style = MaterialTheme.typography.titleLarge,
              fontWeight = FontWeight.Bold
            )
          }
        },
        colors = TopAppBarDefaults.topAppBarColors(
          containerColor = MaterialTheme.colorScheme.primaryContainer
        )
      )
    },
    floatingActionButton = {
      FloatingActionButton(
        onClick = { contactsVM.onAddContactClicked() }
      ) {
        Icon(
          Icons.Default.Add,
          contentDescription = stringResource(id = R.string.add_contact_title)
        )
      }
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
    ) {
      val isConnectionIssue = connectionState != ServiceConnectionState.Online &&
          connectionState != ServiceConnectionState.Establishing &&
          connectionState != ServiceConnectionState.Unknown

      val showPermissionBanner = !isConnectionIssue && bannerState != null

      AnimatedVisibility(
        visible = isConnectionIssue || showPermissionBanner,
        enter = expandVertically(expandFrom = Alignment.Top) + fadeIn(),
        exit = shrinkVertically(shrinkTowards = Alignment.Top) + fadeOut()
      ) {
        Box(modifier = Modifier.padding(16.dp)) {
          if (isConnectionIssue) {
            val (msg, isError) = when (connectionState) {
              ServiceConnectionState.NoNetwork -> context.getString(R.string.connection_no_network) to false
              ServiceConnectionState.Failure -> context.getString(R.string.connection_failed) to true
              ServiceConnectionState.Disconnected -> context.getString(R.string.connection_disconnected) to false
              ServiceConnectionState.InvalidToken -> context.getString(R.string.connection_auth_error) to true
              else -> context.getString(R.string.connection_issue) to true
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
                    val intent =
                      Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                        data = "package:${context.packageName}".toUri()
                      }
                    try {
                      context.startActivity(intent)
                    } catch (e: Exception) {
                      openAppSettings(context)
                    }
                  },
                  dismissLabel = stringResource(id = R.string.action_ignore_permission),
                  onDismiss = { mainViewModel.ignoreBatteryOptimization() }
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
                  onDismiss = { mainViewModel.ignoreCallPermissions() }
                )
              }

              null -> {}
            }
          }
        }
      }

      val visibleContacts = contacts.filter { it.status != ContactStatus.rejected }

      if (isLoading) {
        Box(
          modifier = Modifier
            .weight(1f)
            .fillMaxWidth(),
          contentAlignment = Alignment.Center
        ) {
          CircularProgressIndicator()
        }
      } else if (visibleContacts.isEmpty()) {
        Box(
          modifier = Modifier
            .weight(1f)
            .fillMaxWidth(),
          contentAlignment = Alignment.Center
        ) {
          Text(
            context.getString(R.string.contacts_empty_message),
            style = MaterialTheme.typography.bodyLarge
          )
        }
      } else {
        val locale =
          ConfigurationCompat.getLocales(LocalConfiguration.current)[0] ?: Locale.getDefault()
        val zoneId = ZoneId.systemDefault()
        val formatters = remember(locale) {
          Triple(
            DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT).withLocale(locale),
            DateTimeFormatter.ofPattern("MMM d", locale),
            DateTimeFormatter.ofLocalizedDate(FormatStyle.SHORT).withLocale(locale)
          )
        }

        LazyColumn(modifier = Modifier.weight(1f)) {
          items(visibleContacts, key = { it.peerId }) { contact ->
            ContactListItem(
              contact = contact,
              onClick = { onContactClick(contact.peerId) },
              onShowProfile = { onShowContactProfile(contact.peerId) },
              zoneId = zoneId,
              formatters = formatters
            )
            HorizontalDivider(color = DividerDefaults.color.copy(alpha = 0.5f))
          }
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
