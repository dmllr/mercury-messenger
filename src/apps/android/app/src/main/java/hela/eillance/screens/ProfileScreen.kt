package hela.eillance.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
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
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.MainViewModel
import hela.eillance.camera.R
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.screens.profile.ProfileEditDialog
import hela.eillance.screens.profile.ProfileHeader
import hela.eillance.screens.profile.ProfileMenu
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.DebugViewModel
import hela.eillance.viewmodels.ProfileViewModel
import hela.eillance.viewmodels.SettingsViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProfileScreen(
  profileVM: ProfileViewModel,
  contactsVM: ContactsViewModel,
  settingsVM: SettingsViewModel,
  debugVM: DebugViewModel,
  mainViewModel: MainViewModel,
  myProfile: MyProfileInfo,
  onBack: () -> Unit,
  onNavigateToSettings: () -> Unit,
  onNavigateToBetaNotice: () -> Unit,
  onNavigateToBackupAndRestore: () -> Unit,
  onNavigateToHelp: () -> Unit,
  onNavigateToPermissionIssues: () -> Unit,
  onNavigateToDebugMenu: () -> Unit // New callback
) {
  var showEditDialog by remember { mutableStateOf(false) }
  val hasPermissionIssues by mainViewModel.hasPermissionIssues.collectAsState()

  if (showEditDialog) {
    ProfileEditDialog(
      myProfile = myProfile,
      profileVM = profileVM,
      onDismiss = { showEditDialog = false }
    )
  }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text(stringResource(id = R.string.screen_title_profile)) },
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
        .verticalScroll(rememberScrollState()),
      horizontalAlignment = Alignment.CenterHorizontally
    ) {
      ProfileHeader(
        myProfile = myProfile,
        contactsVM = contactsVM,
        settingsVM = settingsVM
      )
      HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp, horizontal = 16.dp))
      ProfileMenu(
        myProfile = myProfile,
        hasPermissionIssues = hasPermissionIssues,
        onEditProfile = { showEditDialog = true },
        onNavigateToSettings = onNavigateToSettings,
        onNavigateToBetaNotice = onNavigateToBetaNotice,
        onNavigateToBackupAndRestore = onNavigateToBackupAndRestore,
        onNavigateToHelp = onNavigateToHelp,
        onSendFeedback = { contactsVM.sendFeedback() },
        onNavigateToPermissionIssues = onNavigateToPermissionIssues,
        onNavigateToDebugMenu = onNavigateToDebugMenu
      )
    }
  }
}
