package hela.eillance.screens

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Base64
import android.util.Log
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.DeleteForever
import androidx.compose.material.icons.filled.Logout
import androidx.compose.material.icons.filled.Person
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
import androidx.compose.material3.SnackbarHost
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
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import coil.compose.rememberAsyncImagePainter
import hela.eillance.MainViewModel
import hela.eillance.camera.BuildConfig
import hela.eillance.ndkio.AppState
import kotlinx.coroutines.launch
import java.io.ByteArrayOutputStream

private enum class DebugAction {
    ERASE_HISTORY,
    ERASE_CONTACTS,
    REVOKE_APPROVALS
}

@Composable
internal fun rememberImageBitmapFromBase64(base64: String?): ImageBitmap? {
    return remember(base64) {
        if (base64.isNullOrBlank()) {
            null
        } else {
            try {
                val decodedBytes = Base64.decode(base64, Base64.NO_WRAP)
                BitmapFactory.decodeByteArray(decodedBytes, 0, decodedBytes.size)?.asImageBitmap()
            } catch (e: IllegalArgumentException) {
                // Handle invalid Base64 string
                Log.e("ImageBitmap", "Failed to decode base64 string", e)
                null
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ProfileScreen(viewModel: MainViewModel, uiState: AppState, onBack: () -> Unit, onNavigateToSettings: () -> Unit) {
    var showEditDialog by remember { mutableStateOf(false) }
    var showDebugConfirmDialog by remember { mutableStateOf<DebugAction?>(null) }

    if (showEditDialog) {
        ProfileEditDialog(
            uiState = uiState,
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
            ProfileHeader(uiState = uiState)
            HorizontalDivider(modifier = Modifier.padding(vertical = 16.dp, horizontal = 16.dp))
            ProfileMenu(
                onEditProfile = { showEditDialog = true },
                onNavigateToSettings = onNavigateToSettings,
                onDebugAction = { action -> showDebugConfirmDialog = action }
            )
        }
    }
}

@Composable
private fun ProfileHeader(uiState: AppState) {
    val clipboardManager = LocalClipboardManager.current
    val snackbarHostState = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = 24.dp, start = 16.dp, end = 16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Photo
        Box(
            modifier = Modifier
                .size(120.dp)
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center
        ) {
            val imageBitmap = rememberImageBitmapFromBase64(uiState.myPhoto)

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

        Spacer(modifier = Modifier.height(16.dp))

        // Name
        Text(
            text = uiState.myDisplayName ?: uiState.myId,
            style = MaterialTheme.typography.headlineSmall,
            textAlign = TextAlign.Center
        )

        Spacer(modifier = Modifier.height(4.dp))

        // Bio
        if (!uiState.myBio.isNullOrBlank()) {
            Text(
                text = uiState.myBio,
                style = MaterialTheme.typography.bodyLarge,
                textAlign = TextAlign.Center,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(16.dp))
        }

        // Your ID
        Text(
            text = "ID: ${formatId(uiState.myId)}",
            style = MaterialTheme.typography.bodyMedium.copy(fontFamily = FontFamily.Monospace),
            textAlign = TextAlign.Center,
            modifier = Modifier
                .clip(MaterialTheme.shapes.small)
                .clickable {
                    clipboardManager.setText(AnnotatedString(uiState.myId))
                    scope.launch {
                        snackbarHostState.showSnackbar("ID Copied!")
                    }
                }
                .padding(8.dp)
        )
    }
    Box(modifier = Modifier.fillMaxWidth()) {
        SnackbarHost(hostState = snackbarHostState, modifier = Modifier.align(Alignment.BottomCenter))
    }
}

private fun formatId(id: String): String {
    return id.chunked(4).joinToString(" ")
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ProfileMenu(
    onEditProfile: () -> Unit,
    onNavigateToSettings: () -> Unit,
    onDebugAction: (DebugAction) -> Unit
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
                leadingContent = { Icon(Icons.Default.Logout, contentDescription = "Revoke Approvals", tint = MaterialTheme.colorScheme.error) },
                modifier = Modifier
                    .clip(MaterialTheme.shapes.medium)
                    .clickable { onDebugAction(DebugAction.REVOKE_APPROVALS) }
            )
        }
    }
}

@Composable
private fun ProfileEditDialog(
    uiState: AppState,
    viewModel: MainViewModel,
    onDismiss: () -> Unit
) {
    var displayName by remember(uiState.myDisplayName) { mutableStateOf(uiState.myDisplayName ?: "") }
    var bio by remember(uiState.myBio) { mutableStateOf(uiState.myBio ?: "") }
    var photoBase64 by remember(uiState.myPhoto) { mutableStateOf(uiState.myPhoto) }

    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    val imagePickerLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.GetContent()
    ) { uri ->
        uri?.let {
            try {
                val inputStream = context.contentResolver.openInputStream(it)
                val bitmap = BitmapFactory.decodeStream(inputStream)
                val resizedBitmap = Bitmap.createScaledBitmap(bitmap, 512, 512, true)
                val outputStream = ByteArrayOutputStream()
                resizedBitmap.compress(Bitmap.CompressFormat.JPEG, 80, outputStream)
                val byteArray = outputStream.toByteArray()
                photoBase64 = Base64.encodeToString(byteArray, Base64.NO_WRAP)
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
                SnackbarHost(hostState = snackbarHostState)
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
