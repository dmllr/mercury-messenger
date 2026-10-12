package hela.eillance.screens

import android.app.Application
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.util.Base64
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import hela.eillance.camera.R
import hela.eillance.components.VideoPlayer
import hela.eillance.components.ZoomableImage
import hela.eillance.ndkio.Attachment
import hela.eillance.ndkio.AttachmentType
import hela.eillance.utils.shareContent
import hela.eillance.viewmodels.ConversationViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import java.io.File
import java.util.Locale

@kotlin.OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MediaPreviewScreen(
  peerId: String,
  attachmentsJson: String,
  initialIndex: Int,
  onBack: () -> Unit
) {
  val context = LocalContext.current
  val app = context.applicationContext as Application
  val conversationVM: ConversationViewModel = viewModel(
    factory = ConversationViewModel.Factory(app, peerId),
    key = "MediaPreview_$peerId"
  )
  val transferProgress by conversationVM.transferProgress.collectAsState()

  var attachmentsList by remember(attachmentsJson) {
    mutableStateOf(
      try {
        Json.decodeFromString<List<Attachment>>(attachmentsJson)
      } catch (e: Exception) {
        emptyList()
      }
    )
  }

  if (attachmentsList.isEmpty()) {
    LaunchedEffect(Unit) { onBack() }
    return
  }

  val pagerState = rememberPagerState(
    initialPage = initialIndex.coerceIn(0, maxOf(0, attachmentsList.size - 1)),
    pageCount = { attachmentsList.size }
  )

  var showMenu by remember { mutableStateOf(false) }
  var pendingActionAttachment by remember { mutableStateOf<Attachment?>(null) }
  var isMoveAction by remember { mutableStateOf(false) }
  val coroutineScope = rememberCoroutineScope()

  val exportContract = object : ActivityResultContracts.CreateDocument("*/*") {
    override fun createIntent(context: Context, input: String): Intent {
      val parts = input.split("|", limit = 2)
      val mime = parts[0].ifEmpty { "*/*" }
      val name = parts.getOrNull(1) ?: "file"
      return super.createIntent(context, name).setType(mime)
    }
  }

  val exportLauncher = rememberLauncherForActivityResult(exportContract) { uri ->
    if (uri != null && pendingActionAttachment != null) {
      val att = pendingActionAttachment!!
      coroutineScope.launch(Dispatchers.IO) {
        try {
          val file = File(att.path)
          if (file.exists()) {
            context.contentResolver.openOutputStream(uri)?.use { out ->
              file.inputStream().use { input ->
                input.copyTo(out)
              }
            }
            withContext(Dispatchers.Main) {
              Toast.makeText(context, "Saved", Toast.LENGTH_SHORT).show()
              if (isMoveAction) {
                conversationVM.deleteAttachmentLocal(att.id)
                attachmentsList = attachmentsList.filter { it.id != att.id }
              }
            }
          }
        } catch (e: Exception) {
          withContext(Dispatchers.Main) {
            Toast.makeText(context, "Failed to save", Toast.LENGTH_SHORT).show()
          }
        }
      }
    }
    pendingActionAttachment = null
  }

  Scaffold(
    containerColor = Color.Black,
    topBar = {
      TopAppBar(
        title = {
          Text(
            text = "${pagerState.currentPage + 1} / ${attachmentsList.size}",
            style = MaterialTheme.typography.titleMedium,
            color = Color.White
          )
        },
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(
              imageVector = Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = stringResource(id = R.string.action_back)
            )
          }
        },
        actions = {
          Box {
            IconButton(onClick = { showMenu = true }) {
              Icon(Icons.Default.MoreVert, contentDescription = "Options", tint = Color.White)
            }
            DropdownMenu(expanded = showMenu, onDismissRequest = { showMenu = false }) {
              DropdownMenuItem(
                text = { Text(stringResource(id = R.string.action_share)) },
                onClick = {
                  showMenu = false
                  shareContent(context, null, listOf(attachmentsList[pagerState.currentPage]))
                }
              )
              DropdownMenuItem(
                text = { Text(stringResource(id = R.string.action_copy_to)) },
                onClick = {
                  showMenu = false
                  val att = attachmentsList[pagerState.currentPage]
                  pendingActionAttachment = att
                  isMoveAction = false
                  exportLauncher.launch("${att.mime}|${att.fileName.ifEmpty { "file" }}")
                }
              )
              DropdownMenuItem(
                text = { Text(stringResource(id = R.string.action_move_to)) },
                onClick = {
                  showMenu = false
                  val att = attachmentsList[pagerState.currentPage]
                  pendingActionAttachment = att
                  isMoveAction = true
                  exportLauncher.launch("${att.mime}|${att.fileName.ifEmpty { "file" }}")
                }
              )
              DropdownMenuItem(
                text = { Text(stringResource(id = R.string.action_delete)) },
                onClick = {
                  showMenu = false
                  val att = attachmentsList[pagerState.currentPage]
                  conversationVM.deleteAttachmentLocal(att.id)
                  attachmentsList = attachmentsList.filter { it.id != att.id }
                }
              )
            }
          }
        },
        colors = TopAppBarDefaults.topAppBarColors(
          containerColor = Color.Black.copy(alpha = 0.5f),
          navigationIconContentColor = Color.White,
          actionIconContentColor = Color.White,
          titleContentColor = Color.White
        )
      )
    }
  ) { padding ->
    Box(
      modifier = Modifier
        .fillMaxSize()
        .background(Color.Black)
    ) {
      HorizontalPager(
        state = pagerState,
        modifier = Modifier.fillMaxSize(),
        pageSpacing = 0.dp
      ) { page ->
        MediaPage(
          attachment = attachmentsList[page],
          progress = transferProgress[attachmentsList[page].id],
          vm = conversationVM,
          isVisible = pagerState.currentPage == page
        )
      }

      // Note Overlay
      val currentAttachment = attachmentsList.getOrNull(pagerState.currentPage)
      if (currentAttachment?.note?.isNotEmpty() == true) {
        Box(
          modifier = Modifier
            .align(Alignment.BottomCenter)
            .fillMaxWidth()
            .background(
              Brush.verticalGradient(
                colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.8f))
              )
            )
            .padding(16.dp)
            .padding(bottom = 24.dp)
        ) {
          Text(
            text = currentAttachment.note,
            style = MaterialTheme.typography.bodyLarge,
            color = Color.White
          )
        }
      }
    }
  }
}

@Composable
fun MediaPage(
  attachment: Attachment,
  progress: Float?,
  vm: ConversationViewModel,
  isVisible: Boolean
) {
  var currentAttachment by remember { mutableStateOf(attachment) }

  // If the thumbnail was stripped during navigation (to avoid TransactionTooLargeException),
  // fetch the full attachment details from the database.
  LaunchedEffect(attachment.id) {
    if (currentAttachment.thumbnail.isNullOrEmpty()) {
      val fullData = vm.getAttachment(attachment.id)
      if (fullData != null) {
        currentAttachment = fullData
      }
    }
  }

  val file = File(currentAttachment.path)

  Box(modifier = Modifier.fillMaxSize()) {
    // Show thumbnail background if file is missing or loading
    val imageBitmap = remember(currentAttachment.thumbnail) {
      if (!currentAttachment.thumbnail.isNullOrEmpty()) {
        try {
          val bytes = Base64.decode(currentAttachment.thumbnail, Base64.DEFAULT)
          BitmapFactory.decodeByteArray(bytes, 0, bytes.size)?.asImageBitmap()
        } catch (e: Exception) {
          null
        }
      } else null
    }

    if (!file.exists()) {
      if (imageBitmap != null) {
        Image(
          bitmap = imageBitmap,
          contentDescription = null,
          modifier = Modifier
            .fillMaxSize()
            .graphicsLayer { alpha = 0.7f },
          contentScale = ContentScale.Crop
        )
      }

      if (progress != null) {
        CircularProgressIndicator(
          progress = { progress },
          modifier = Modifier.align(Alignment.Center),
          color = Color.White
        )
      } else {
        val errorText = when (currentAttachment.type) {
          AttachmentType.Image -> stringResource(R.string.media_error_image)
          AttachmentType.Video -> stringResource(R.string.media_error_video)
          else -> stringResource(R.string.media_error_file)
        }

        // Dark overlay to make text readable over thumbnail
        Box(
          modifier = Modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.3f))
        )

        Text(
          text = errorText,
          color = Color.White,
          style = MaterialTheme.typography.bodyLarge,
          modifier = Modifier
            .align(Alignment.Center)
            .padding(16.dp)
        )
      }
    } else {
      val isVideo = currentAttachment.type == AttachmentType.Video ||
          currentAttachment.mime.startsWith("video/") ||
          file.extension.lowercase(Locale.ROOT) in setOf("mp4", "mkv", "webm", "avi", "mov", "3gp")

      if (isVideo) {
        VideoPlayer(file, isVisible)
      } else {
        ZoomableImage(file)
      }
    }
  }
}
