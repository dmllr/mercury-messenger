package hela.eillance.screens

import android.app.Application
import android.graphics.BitmapFactory
import android.util.Base64
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
import androidx.compose.material3.CircularProgressIndicator
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
import hela.eillance.viewmodels.ConversationViewModel
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

  val attachments = remember(attachmentsJson) {
    try {
      Json.decodeFromString<List<Attachment>>(attachmentsJson)
    } catch (e: Exception) {
      emptyList()
    }
  }

  if (attachments.isEmpty()) {
    onBack()
    return
  }

  val pagerState = rememberPagerState(initialPage = initialIndex, pageCount = { attachments.size })

  Scaffold(
    containerColor = Color.Black,
    topBar = {
      TopAppBar(
        title = {
          Text(
            text = "${pagerState.currentPage + 1} / ${attachments.size}",
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
          attachment = attachments[page],
          progress = transferProgress[attachments[page].id],
          vm = conversationVM,
          isVisible = pagerState.currentPage == page
        )
      }

      // Note Overlay
      val currentAttachment = attachments[pagerState.currentPage]
      if (currentAttachment.note.isNotEmpty()) {
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
