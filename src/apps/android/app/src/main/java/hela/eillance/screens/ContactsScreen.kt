package hela.eillance.screens

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
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
import hela.eillance.screens.conversation.formatTimestamp
import hela.eillance.viewmodels.ContactsViewModel
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

@SuppressLint("BatteryLife")
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ContactsScreen(
  contactsVM: ContactsViewModel,
  mainViewModel: MainViewModel,
  myProfile: MyProfileInfo,
  contacts: List<Contact>,
  isLoading: Boolean,
  connectionState: ServiceConnectionState,
  isCoreUnresponsive: Boolean,
  isInCall: Boolean,
  onResetCore: () -> Unit,
  onCheckPermissions: () -> Unit,
  onContactClick: (String, Long?) -> Unit,
  onNavigateToProfile: () -> Unit,
  onNavigateToSettings: () -> Unit,
  onShowContactProfile: (String) -> Unit
) {
  val snackbarHostState = remember { SnackbarHostState() }
  val context = LocalContext.current
  val bannerState by mainViewModel.permissionBannerState.collectAsState()
  val hasPermissionIssues by mainViewModel.hasPermissionIssues.collectAsState()
  val pendingShare by mainViewModel.pendingShare.collectAsState()

  // Search State
  var isSearchActive by rememberSaveable { mutableStateOf(false) }
  var searchQuery by rememberSaveable { mutableStateOf("") }
  val searchResults by contactsVM.searchResults.collectAsState()
  val focusRequester = remember { FocusRequester() }
  val focusManager = LocalFocusManager.current

  val notifLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { onCheckPermissions() }
  val callLauncher =
    rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { onCheckPermissions() }

  LaunchedEffect(contactsVM) {
    contactsVM.errorMessage.collect { message ->
      snackbarHostState.showSnackbar(message)
    }
  }

  // Handle back press to close search
  BackHandler(enabled = isSearchActive) {
    isSearchActive = false
    searchQuery = ""
    contactsVM.searchAllMessages("")
  }

  val hasConnectionIssues = connectionState == ServiceConnectionState.Failure ||
      connectionState == ServiceConnectionState.Disconnected ||
      connectionState == ServiceConnectionState.NoNetwork ||
      connectionState == ServiceConnectionState.InvalidToken

  LaunchedEffect(isSearchActive) {
    if (isSearchActive) {
      focusRequester.requestFocus()
    } else {
      focusManager.clearFocus()
    }
  }

  Scaffold(
    snackbarHost = { SnackbarHost(snackbarHostState) },
    topBar = {
      if (isSearchActive) {
        TopAppBar(
          title = {
            TextField(
              value = searchQuery,
              onValueChange = {
                searchQuery = it
                contactsVM.searchAllMessages(it)
              },
              placeholder = { Text(stringResource(R.string.search_hint)) },
              modifier = Modifier
                .fillMaxWidth()
                .focusRequester(focusRequester),
              colors = TextFieldDefaults.colors(
                focusedContainerColor = Color.Transparent,
                unfocusedContainerColor = Color.Transparent,
                focusedIndicatorColor = Color.Transparent,
                unfocusedIndicatorColor = Color.Transparent
              ),
              singleLine = true,
              keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
              keyboardActions = KeyboardActions(onSearch = { focusManager.clearFocus() })
            )
          },
          navigationIcon = {
            IconButton(onClick = {
              isSearchActive = false
              searchQuery = ""
              contactsVM.searchAllMessages("")
            }) {
              Icon(
                Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = stringResource(R.string.action_back)
              )
            }
          },
          actions = {
            if (searchQuery.isNotEmpty()) {
              IconButton(onClick = {
                searchQuery = ""
                contactsVM.searchAllMessages("")
                focusRequester.requestFocus()
              }) {
                Icon(
                  Icons.Default.Close,
                  contentDescription = stringResource(R.string.action_close)
                )
              }
            }
          },
          colors = TopAppBarDefaults.topAppBarColors(
            containerColor = MaterialTheme.colorScheme.surfaceContainer
          )
        )
      } else {
        TopAppBar(
          title = {
            Text(
              text = if (pendingShare != null) {
                stringResource(id = R.string.share_with)
              } else {
                "\uD835\uDCDC\uD835\uDCEE\uD835\uDCFB\uD835\uDCEC\uD835\uDCFE\uD835\uDCFB\uD835\uDD02"
              },
              style = MaterialTheme.typography.titleLarge,
              fontWeight = FontWeight.Bold
            )
          },
          actions = {
          },
        )
      }
    }
  ) { padding ->
    Column(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
    ) {
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

      if (isSearchActive) {
        if (searchResults.isEmpty() && searchQuery.isNotEmpty()) {
          Box(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
          ) {
            Text(stringResource(R.string.search_no_results))
          }
        } else {
          LazyColumn {
            items(searchResults) { result ->
              // Find contact for name resolution
              val contact = contacts.find { it.peerId == result.peerId }
              val name = contact?.name ?: result.peerId

              Column(
                modifier = Modifier
                  .fillMaxWidth()
                  .clickable { onContactClick(result.peerId, result.entry.id) }
                  .padding(16.dp)
              ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                  UserAvatar(photoBase64 = contact?.photo, modifier = Modifier.size(32.dp))
                  Text(
                    text = name,
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(start = 12.dp)
                  )
                  Text(
                    text = " • " + formatTimestamp(result.entry.timestamp, zoneId, formatters),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                  )
                }
                Text(
                  text = result.entry.content ?: "",
                  style = MaterialTheme.typography.bodyMedium,
                  color = MaterialTheme.colorScheme.onSurfaceVariant,
                  maxLines = 2,
                  overflow = TextOverflow.Ellipsis,
                  modifier = Modifier.padding(top = 4.dp, start = 44.dp)
                )
              }
            }
          }
        }
      } else {
        // Global Error Snackbar (from ViewModel events)
        val globalError by contactsVM.errorMessage.collectAsState(initial = null)
        if (globalError != null) {
          StatusSnackbar(text = globalError!!, isError = true)
        }

        // Active Call Snackbar
        // Note: The main app logic forces the call screen to top, so this is defensively implemented
        // in case navigation allows reaching contacts screen during call in future.
        if (isInCall) {
          StatusSnackbar(
            text = stringResource(id = R.string.return_to_call),
            actionLabel = stringResource(id = R.string.action_open),
            onAction = {
              // Action is handled by global state: MainViewModel will trigger nav or CallScreen will show.
              // Here we can just ensure we don't block.
            },
            isError = false
          )
        }

        val isConnectionIssue = !isCoreUnresponsive &&
            (connectionState != ServiceConnectionState.Online &&
                connectionState != ServiceConnectionState.Establishing &&
                connectionState != ServiceConnectionState.Unknown)

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
                      val intent =
                        @SuppressLint("BatteryLife")
                        Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS).apply {
                          data = "package:${context.packageName}".toUri()
                        }
                      try {
                        context.startActivity(intent)
                      } catch (_: Exception) {
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
        } else {
          Box(modifier = Modifier
            .weight(1f)
            .fillMaxWidth()) {
            LazyColumn(
              modifier = Modifier
                .fillMaxSize()
                .padding(2.dp),
              contentPadding = PaddingValues(bottom = 88.dp)
            ) {
              items(visibleContacts, key = { it.peerId }) { contact ->
                ContactListItem(
                  contact = contact,
                  onClick = { onContactClick(contact.peerId, null) },
                  onShowProfile = { onShowContactProfile(contact.peerId) },
                  zoneId = zoneId,
                  formatters = formatters
                )
              }
              item {
                androidx.compose.material3.ListItem(
                  headlineContent = {
                    Text(
                      stringResource(R.string.add_contact_title),
                      style = MaterialTheme.typography.bodyLarge,
                      fontWeight = FontWeight.SemiBold
                    )
                  },
                  leadingContent = {
                    Box(
                      modifier = Modifier
                        .size(48.dp)
                        .background(MaterialTheme.colorScheme.secondaryContainer, CircleShape),
                      contentAlignment = Alignment.Center
                    ) {
                      Icon(
                        Icons.Default.Add,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSecondaryContainer
                      )
                    }
                  },
                  modifier = Modifier.clickable { contactsVM.onAddContactClicked() },
                  colors = androidx.compose.material3.ListItemDefaults.colors(containerColor = Color.Transparent)
                )
              }
            }

            if (!isSearchActive) {
              Surface(
                modifier = Modifier
                  .align(Alignment.BottomCenter)
                  .padding(bottom = 16.dp)
                  .navigationBarsPadding(),
                shape = RoundedCornerShape(percent = 50),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
                shadowElevation = 6.dp
              ) {
                Row(
                  modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
                  verticalAlignment = Alignment.CenterVertically,
                  horizontalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                  IconButton(onClick = { isSearchActive = true }) {
                    Icon(Icons.Default.Search, contentDescription = stringResource(R.string.action_search))
                  }
                  IconButton(onClick = onNavigateToSettings) {
                    Icon(Icons.Default.Settings, contentDescription = stringResource(R.string.screen_title_settings))
                  }
                  Box {
                    IconButton(
                      onClick = onNavigateToProfile,
                      modifier = Modifier.size(48.dp)
                    ) {
                      UserAvatar(photoBase64 = myProfile.myPhoto, modifier = Modifier.size(40.dp))
                    }
                    if (hasPermissionIssues || hasConnectionIssues) {
                      Box(
                        modifier = Modifier
                          .align(Alignment.TopEnd)
                          .padding(top = 4.dp, end = 4.dp)
                          .size(10.dp)
                          .background(MaterialTheme.colorScheme.error, CircleShape)
                          .border(1.dp, MaterialTheme.colorScheme.surfaceContainerHigh, CircleShape)
                      )
                    }
                  }
                  Box {
                    var menuExpanded by remember { mutableStateOf(false) }
                    IconButton(onClick = { menuExpanded = true }) {
                      Icon(Icons.Default.Add, contentDescription = stringResource(R.string.add_contact_title))
                    }
                    DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                      DropdownMenuItem(
                        text = { Text(stringResource(R.string.add_a_new_contact)) },
                        onClick = { menuExpanded = false; contactsVM.onAddContactClicked() }
                      )
                    }
                  }
                }
              }
            }
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
