package hela.eillance.screens.profile

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Help
import androidx.compose.material.icons.filled.Feedback
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.components.UserAvatar
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.theme.ListItem

@Composable
fun ProfileMenu(
  myProfile: MyProfileInfo,
  hasPermissionIssues: Boolean,
  hasConnectionIssues: Boolean,
  onEditProfile: () -> Unit,
  onNavigateToSettings: () -> Unit,
  onNavigateToBetaNotice: () -> Unit,
  onNavigateToBackupAndRestore: () -> Unit,
  onNavigateToHelp: () -> Unit,
  onSendFeedback: () -> Unit,
  onNavigateToPermissionIssues: () -> Unit
) {
  Column(modifier = Modifier.padding(horizontal = 8.dp)) {
    ListItem(
      headlineContent = { Text(stringResource(id = R.string.profile_menu_my_profile_title)) },
      supportingContent = { Text(stringResource(id = R.string.profile_menu_my_profile_description)) },
      leadingContent = {
        if (!myProfile.myPhoto.isNullOrBlank()) {
          UserAvatar(
            photoBase64 = myProfile.myPhoto,
            modifier = Modifier.size(24.dp)
          )
        } else {
          Icon(
            Icons.Default.Person,
            contentDescription = stringResource(id = R.string.profile_menu_my_profile_title)
          )
        }
      },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onEditProfile)
    )

    if (hasPermissionIssues || hasConnectionIssues) {
      ListItem(
        headlineContent = { Text(stringResource(id = R.string.screen_title_permission_issues)) },
        supportingContent = { Text(stringResource(id = R.string.profile_menu_permission_issues_description)) },
        leadingContent = {
          Icon(
            Icons.Default.Warning,
            contentDescription = stringResource(id = R.string.profile_menu_permission_issues_icon_description),
            tint = MaterialTheme.colorScheme.error
          )
        },
        trailingContent = {
          Box(
            modifier = Modifier
              .size(8.dp)
              .background(MaterialTheme.colorScheme.error, CircleShape)
          )
        },
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable(onClick = onNavigateToPermissionIssues)
      )
    }

    ListItem(
      headlineContent = { Text(stringResource(id = R.string.screen_title_settings)) },
      supportingContent = { Text(stringResource(id = R.string.profile_menu_settings_description)) },
      leadingContent = {
        Icon(
          Icons.Default.Settings,
          contentDescription = stringResource(id = R.string.screen_title_settings)
        )
      },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onNavigateToSettings)
    )
    ListItem(
      headlineContent = { Text(stringResource(id = R.string.profile_menu_beta_title)) },
      supportingContent = { Text(stringResource(id = R.string.profile_menu_beta_description)) },
      leadingContent = {
        Icon(
          Icons.Default.Info,
          contentDescription = stringResource(id = R.string.profile_menu_beta_title)
        )
      },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onNavigateToBetaNotice)
    )
    ListItem(
      headlineContent = { Text(stringResource(id = R.string.screen_title_help)) },
      supportingContent = { Text(stringResource(id = R.string.profile_menu_help_description)) },
      leadingContent = {
        Icon(
          Icons.AutoMirrored.Filled.Help,
          contentDescription = stringResource(id = R.string.screen_title_help)
        )
      },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onNavigateToHelp)
    )
    ListItem(
      headlineContent = { Text(stringResource(id = R.string.profile_menu_feedback_title)) },
      supportingContent = { Text(stringResource(id = R.string.profile_menu_feedback_description)) },
      leadingContent = {
        Icon(
          Icons.Default.Feedback,
          contentDescription = stringResource(id = R.string.profile_menu_feedback_title)
        )
      },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onSendFeedback)
    )
    ListItem(
      headlineContent = { Text(stringResource(id = R.string.screen_title_backup_restore)) },
      supportingContent = { Text(stringResource(id = R.string.profile_menu_backup_description)) },
      leadingContent = {
        Icon(
          Icons.Filled.Save,
          contentDescription = stringResource(id = R.string.screen_title_backup_restore)
        )
      },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onNavigateToBackupAndRestore)
    )
  }
}
