package hela.eillance.screens

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Color
import android.util.Base64
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Help
import androidx.compose.material.icons.automirrored.filled.Logout
import androidx.compose.material.icons.filled.DeleteForever
import androidx.compose.material.icons.filled.FileCopy
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.QrCode2
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.navigation.NavController
import com.google.zxing.BarcodeFormat
import com.google.zxing.EncodeHintType
import com.google.zxing.qrcode.QRCodeWriter
import com.google.zxing.qrcode.decoder.ErrorCorrectionLevel
import hela.eillance.MainViewModel
import hela.eillance.camera.BuildConfig
import hela.eillance.ndkio.MyProfileInfo
import hela.eillance.utils.rememberImageBitmapFromBase64
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream

private enum class DebugAction {
  ERASE_HISTORY,
  ERASE_CONTACTS,
  REVOKE_APPROVALS
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProfileScreen(
  viewModel: MainViewModel,
  myProfile: MyProfileInfo,
  onBack: () -> Unit,
  onNavigateToSettings: () -> Unit,
  navController: NavController,
  onNavigateToBackupAndRestore: () -> Unit,
  onNavigateToHelp: () -> Unit
) {
  var showEditDialog by remember { mutableStateOf(false) }
  var showDebugConfirmDialog by remember { mutableStateOf<DebugAction?>(null) }

  if (showEditDialog) {
    ProfileEditDialog(
      myProfile = myProfile,
      viewModel = viewModel,
      onDismiss = { showEditDialog = false }
    )
  }

  showDebugConfirmDialog?.let { action ->
    DebugConfirmDialog(
      action = action,
      onConfirm = { confirmedAction ->
        when(confirmedAction) {
          DebugAction.ERASE_HISTORY -> viewModel.debugEraseAllHistory()
          DebugAction.ERASE_CONTACTS -> viewModel.debugEraseAllContacts()
          DebugAction.REVOKE_APPROVALS -> viewModel.debugRevokeAllApprovals()
        }
        showDebugConfirmDialog = null
      },
      onDismiss = { showDebugConfirmDialog = null }
    )
  }

  Scaffold(
    topBar = {
      TopAppBar(
        title = { Text("Profile") },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = "Back")
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
      ProfileHeader(myProfile = myProfile)
      HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp, horizontal = 16.dp))
      ProfileMenu(
        onEditProfile = { showEditDialog = true },
        onNavigateToSettings = onNavigateToSettings,
        onNavigateToBackupAndRestore = onNavigateToBackupAndRestore,
        onDebugAction = { action: DebugAction -> showDebugConfirmDialog = action },
        onNavigateToHelp = onNavigateToHelp
      )
    }
  }
}

@Composable
private fun ProfileHeader(myProfile: MyProfileInfo) {
  val context = LocalContext.current
  var showQrDialog by remember { mutableStateOf(false) }
  val clipboardManager = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager

  val copyIdToClipboard: () -> Unit = {
    clipboardManager.setPrimaryClip(ClipData.newPlainText("User ID", myProfile.myId))
    Toast.makeText(context, "ID copied to clipboard", Toast.LENGTH_SHORT).show()
  }

  Column(
    modifier = Modifier
      .fillMaxWidth()
      .padding(top = 24.dp, start = 16.dp, end = 16.dp),
    horizontalAlignment = Alignment.CenterHorizontally
  ) {
    if (showQrDialog) {
      QrCodeDialog(
        profileId = myProfile.myId,
        onDismiss = { showQrDialog = false }
      )
    }

    Box(contentAlignment = Alignment.Center) {
      Box(
        modifier = Modifier
          .size(120.dp)
          .clip(CircleShape)
          .background(MaterialTheme.colorScheme.surfaceVariant),
        contentAlignment = Alignment.Center
      ) {
        val imageBitmap = rememberImageBitmapFromBase64(myProfile.myPhoto)

        if (imageBitmap != null) {
          Image(
            bitmap = imageBitmap,
            contentDescription = "Profile Photo",
            modifier = Modifier.fillMaxSize(),
            contentScale = ContentScale.Crop
          )
        } else {
          Icon(
            imageVector = Icons.Default.Person,
            contentDescription = "Default Profile Icon",
            modifier = Modifier.size(72.dp),
            tint = MaterialTheme.colorScheme.onSurfaceVariant
          )
        }
      }

      IconButton(
        onClick = { showQrDialog = true },
        modifier = Modifier
          .align(Alignment.BottomEnd)
          .size(48.dp)
          .clip(CircleShape)
          .background(MaterialTheme.colorScheme.secondaryContainer)
          .border(2.dp, MaterialTheme.colorScheme.background, CircleShape)
      ) {
        Icon(
          Icons.Default.QrCode2,
          contentDescription = "Show QR Code",
          tint = MaterialTheme.colorScheme.onSecondaryContainer
        )
      }
    }

    Spacer(modifier = Modifier.height(16.dp))

    if (!myProfile.myDisplayName.isNullOrBlank()) {
      Text(
        text = myProfile.myDisplayName,
        style = MaterialTheme.typography.headlineSmall,
        textAlign = TextAlign.Center
      )
      Spacer(modifier = Modifier.height(4.dp))
    }

    if (!myProfile.myBio.isNullOrBlank()) {
      Text(
        text = myProfile.myBio,
        style = MaterialTheme.typography.bodyLarge,
        textAlign = TextAlign.Center,
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
      Spacer(modifier = Modifier.height(16.dp))
    }

    Row(
      modifier = Modifier.padding(top = 8.dp),
      verticalAlignment = Alignment.CenterVertically,
      horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
      Text(
        text = "ID:",
      )

      Text(
        text = formatId(myProfile.myId),
        maxLines = 4,
        style = MaterialTheme.typography.bodyMedium.copy(
          fontFamily = FontFamily.Monospace,
          fontWeight = FontWeight.Bold
        ),
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable(onClick = copyIdToClipboard)
          .padding(horizontal = 12.dp, vertical = 8.dp)
          .widthIn(max = 200.dp)
      )

      IconButton(onClick = copyIdToClipboard) {
        Icon(
          Icons.Default.FileCopy,
          contentDescription = "Copy ID",
          tint = MaterialTheme.colorScheme.primary
        )
      }
    }
  }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ProfileMenu(
  onEditProfile: () -> Unit,
  onNavigateToSettings: () -> Unit,
  onNavigateToBackupAndRestore: () -> Unit,
  onDebugAction: (DebugAction) -> Unit,
  onNavigateToHelp: () -> Unit
) {
  Column(modifier = Modifier.padding(horizontal = 8.dp)) {
    ListItem(
      headlineContent = { Text("My Profile") },
      supportingContent = { Text("Edit your name, photo, and bio") },
      leadingContent = { Icon(Icons.Default.Person, contentDescription = "My Profile") },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onEditProfile)
    )
    ListItem(
      headlineContent = { Text("Settings") },
      supportingContent = { Text("Connection and application settings") },
      leadingContent = { Icon(Icons.Default.Settings, contentDescription = "Settings") },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onNavigateToSettings)
    )
    ListItem(
      headlineContent = { Text("Help") },
      supportingContent = { Text("Learn more about Mercury") },
      leadingContent = { Icon(Icons.AutoMirrored.Filled.Help, contentDescription = "Help") },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onNavigateToHelp)
    )
    ListItem(
      headlineContent = { Text("Backup & Restore") },
      supportingContent = { Text("Create encrypted backups or restore your data") },
      leadingContent = { Icon(Icons.Filled.Save, contentDescription = "Backup") },
      modifier = Modifier
        .clip(MaterialTheme.shapes.medium)
        .clickable(onClick = onNavigateToBackupAndRestore)
    )

    if (BuildConfig.DEBUG) {
      Spacer(modifier = Modifier.height(16.dp))
      HorizontalDivider(modifier = Modifier.padding(horizontal = 8.dp))
      Spacer(modifier = Modifier.height(8.dp))
      Text(
        "Debug Options",
        style = MaterialTheme.typography.titleSmall,
        modifier = Modifier.padding(start = 16.dp, bottom = 8.dp),
        color = MaterialTheme.colorScheme.onSurfaceVariant
      )
      ListItem(
        headlineContent = { Text("Erase all conversation history", color = MaterialTheme.colorScheme.error) },
        leadingContent = { Icon(Icons.Default.DeleteForever, contentDescription = "Erase History", tint = MaterialTheme.colorScheme.error) },
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable { onDebugAction(DebugAction.ERASE_HISTORY) }
      )
      ListItem(
        headlineContent = { Text("Erase contact list", color = MaterialTheme.colorScheme.error) },
        leadingContent = { Icon(Icons.Default.DeleteForever, contentDescription = "Erase Contacts", tint = MaterialTheme.colorScheme.error) },
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable { onDebugAction(DebugAction.ERASE_CONTACTS) }
      )
      ListItem(
        headlineContent = { Text("Revoke contact approvals/accepts", color = MaterialTheme.colorScheme.error) },
        leadingContent = { Icon(Icons.AutoMirrored.Filled.Logout, contentDescription = "Revoke Approvals", tint = MaterialTheme.colorScheme.error) },
        modifier = Modifier
          .clip(MaterialTheme.shapes.medium)
          .clickable { onDebugAction(DebugAction.REVOKE_APPROVALS) }
      )
    }
  }
}

@Composable
private fun ProfileEditDialog(
  myProfile: MyProfileInfo,
  viewModel: MainViewModel,
  onDismiss: () -> Unit
) {
  var displayName by remember(myProfile.myDisplayName) { mutableStateOf(myProfile.myDisplayName ?: "") }
  var bio by remember(myProfile.myBio) { mutableStateOf(myProfile.myBio ?: "") }
  var photoBase64 by remember(myProfile.myPhoto) { mutableStateOf(myProfile.myPhoto) }

  val context = LocalContext.current
  val scope = rememberCoroutineScope()
  val snackbarHostState = remember { SnackbarHostState() }

  val imagePickerLauncher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.GetContent()
  ) { uri ->
    uri?.let {
      try {
        context.contentResolver.openInputStream(it)?.use { inputStream ->
          val bitmap = BitmapFactory.decodeStream(inputStream)
          val resizedBitmap = Bitmap.createScaledBitmap(bitmap, 512, 512, true)
          val outputStream = ByteArrayOutputStream()
          resizedBitmap.compress(Bitmap.CompressFormat.JPEG, 80, outputStream)
          val byteArray = outputStream.toByteArray()
          photoBase64 = Base64.encodeToString(byteArray, Base64.NO_WRAP)
        }
      } catch (e: Exception) {
        e.printStackTrace()
        scope.launch { snackbarHostState.showSnackbar("Failed to load image.") }
      }
    }
  }

  Dialog(onDismissRequest = onDismiss) {
    Card(
      shape = MaterialTheme.shapes.large,
      modifier = Modifier.fillMaxWidth()
    ) {
      Column(
        modifier = Modifier
          .padding(16.dp)
          .verticalScroll(rememberScrollState()),
        horizontalAlignment = Alignment.CenterHorizontally
      ) {
        Text("Edit Profile", style = MaterialTheme.typography.headlineSmall, modifier = Modifier.padding(bottom = 16.dp))
        Box(
          modifier = Modifier
            .size(150.dp)
            .clip(CircleShape)
            .border(2.dp, MaterialTheme.colorScheme.primary, CircleShape)
            .clickable { imagePickerLauncher.launch("image/*") },
          contentAlignment = Alignment.Center
        ) {
          val imageBitmap = rememberImageBitmapFromBase64(photoBase64)
          if (imageBitmap != null) {
            Image(
              bitmap = imageBitmap,
              contentDescription = "Profile Photo",
              modifier = Modifier.fillMaxSize(),
              contentScale = ContentScale.Crop
            )
          } else {
            Icon(
              imageVector = Icons.Default.Person,
              contentDescription = "Default Profile Icon",
              modifier = Modifier
                .size(80.dp)
                .background(MaterialTheme.colorScheme.secondaryContainer, CircleShape)
                .padding(16.dp),
              tint = MaterialTheme.colorScheme.onSecondaryContainer
            )
          }
        }
        Text(
          "Tap to change photo",
          style = MaterialTheme.typography.bodySmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
          modifier = Modifier.padding(top = 4.dp)
        )

        Spacer(modifier = Modifier.height(24.dp))

        OutlinedTextField(
          value = displayName,
          onValueChange = { if (it.length <= 64) displayName = it },
          label = { Text("Display Name") },
          modifier = Modifier.fillMaxWidth(),
          singleLine = true
        )

        Spacer(modifier = Modifier.height(16.dp))

        OutlinedTextField(
          value = bio,
          onValueChange = { if (it.length <= 300) bio = it },
          label = { Text("Short Bio") },
          modifier = Modifier
            .fillMaxWidth()
            .height(120.dp),
          maxLines = 5
        )

        Spacer(modifier = Modifier.height(24.dp))

        Row(
          modifier = Modifier.fillMaxWidth(),
          horizontalArrangement = Arrangement.End
        ) {
          TextButton(onClick = onDismiss) {
            Text("Cancel")
          }
          Spacer(modifier = Modifier.width(8.dp))
          Button(onClick = {
            viewModel.updateMyProfile(displayName, bio, photoBase64)
            onDismiss()
          }) {
            Text("Save")
          }
        }
      }
    }
  }
}

@Composable
private fun DebugConfirmDialog(
  action: DebugAction,
  onConfirm: (DebugAction) -> Unit,
  onDismiss: () -> Unit
) {
  val (title, text) = when (action) {
    DebugAction.ERASE_HISTORY -> "Erase History?" to "This will permanently delete all message and call history for all contacts. This action cannot be undone."
    DebugAction.ERASE_CONTACTS -> "Erase Contacts?" to "This will permanently delete all contacts and all associated conversation history. This action cannot be undone."
    DebugAction.REVOKE_APPROVALS -> "Revoke Approvals?" to "This will reset the status of all 'confirmed' contacts back to 'pending'. You will need to re-confirm them."
  }

  Dialog(onDismissRequest = onDismiss) {
    Card(shape = MaterialTheme.shapes.large) {
      Column(modifier = Modifier.padding(24.dp)) {
        Text(title, style = MaterialTheme.typography.headlineSmall)
        Spacer(modifier = Modifier.height(16.dp))
        Text(text, style = MaterialTheme.typography.bodyMedium)
        Spacer(modifier = Modifier.height(24.dp))
        Row(
          modifier = Modifier.fillMaxWidth(),
          horizontalArrangement = Arrangement.End
        ) {
          TextButton(onClick = onDismiss) {
            Text("Cancel")
          }
          Spacer(modifier = Modifier.width(8.dp))
          Button(
            onClick = { onConfirm(action) },
            colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.error)
          ) {
            Text("Confirm")
          }
        }
      }
    }
  }
}

private fun formatId(id: String): String {
  if (id.isEmpty()) return ""
  return id.chunked(4).joinToString(" ")
}

private fun generateQrCodeBitmap(content: String): ImageBitmap {
  val size = 512
  val hints = mapOf(
    EncodeHintType.ERROR_CORRECTION to ErrorCorrectionLevel.H,
    EncodeHintType.MARGIN to 1
  )
  val bitMatrix = QRCodeWriter().encode(content, BarcodeFormat.QR_CODE, size, size, hints)
  val pixels = IntArray(size * size)
  for (y in 0 until size) {
    for (x in 0 until size) {
      pixels[y * size + x] = if (bitMatrix[x, y]) Color.BLACK else Color.WHITE
    }
  }
  val bitmap = android.graphics.Bitmap.createBitmap(size, size, android.graphics.Bitmap.Config.ARGB_8888)
  bitmap.setPixels(pixels, 0, size, 0, 0, size, size)
  return bitmap.asImageBitmap()
}

@Composable
private fun QrCodeDialog(profileId: String, onDismiss: () -> Unit) {
  val qrContent = remember(profileId) {
    "mercury:contact:$profileId"
  }
  Dialog(onDismissRequest = onDismiss) {
    Card(
      modifier = Modifier.padding(16.dp),
      shape = MaterialTheme.shapes.large
    ) {
      Column(modifier = Modifier.padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        val qrBitmap = remember(qrContent) { generateQrCodeBitmap(qrContent) }
        Image(bitmap = qrBitmap, contentDescription = "Profile ID QR Code", modifier = Modifier.size(256.dp))
        Spacer(Modifier.height(16.dp))
        TextButton(onClick = onDismiss) { Text("Close") }
      }
    }
  }
}
