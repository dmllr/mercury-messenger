package hela.eillance.screens

import android.app.Application
import android.net.Uri
import android.view.ViewGroup
import androidx.annotation.OptIn
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
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
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import coil.compose.AsyncImage
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
              contentDescription = "Back"
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
        MediaPage(attachment = attachments[page], progress = transferProgress[attachments[page].id])
      }
    }
  }
}

@Composable
fun MediaPage(attachment: Attachment, progress: Float?) {
  val file = File(attachment.path)

  Box(modifier = Modifier.fillMaxSize()) {
    if (!file.exists()) {
      if (progress != null) {
        CircularProgressIndicator(
          progress = { progress },
          modifier = Modifier.align(Alignment.Center),
          color = Color.White
        )
      } else {
        Text(
          text = "Image removed",
          color = Color.White,
          modifier = Modifier.align(Alignment.Center)
        )
      }
    } else {
      val isVideo = attachment.type == AttachmentType.Video ||
          attachment.mime.startsWith("video/") ||
          file.extension.lowercase(Locale.ROOT) in setOf("mp4", "mkv", "webm", "avi", "mov", "3gp")

      if (isVideo) {
        VideoPlayer(file)
      } else {
        ZoomableImage(file)
      }
    }
  }
}

@Composable
fun ZoomableImage(file: File) {
  var scale by remember { mutableFloatStateOf(1f) }
  var offset by remember { mutableStateOf(Offset.Zero) }
  var size by remember { mutableStateOf(IntSize.Zero) }

  Box(
    modifier = Modifier
      .fillMaxSize()
      .onSizeChanged { size = it }
      .pointerInput(Unit) {
        detectTapGestures(
          onDoubleTap = {
            if (scale > 1f) {
              scale = 1f
              offset = Offset.Zero
            } else {
              scale = 2.5f
              offset = Offset.Zero
            }
          }
        )
      }
      .pointerInput(Unit) {
        awaitEachGesture {
          awaitFirstDown(requireUnconsumed = false)

          do {
            val event = awaitPointerEvent()
            val canceled = event.changes.any { it.isConsumed }
            if (!canceled) {
              val zoomChange = event.calculateZoom()
              val panChange = event.calculatePan()

              // Determine if we should consume the event.
              // We consume if:
              // 1. We are actively zooming (multi-touch)
              // 2. We are already zoomed in (scale > 1)
              val isMultiTouch = event.changes.size > 1
              val isZoomed = scale > 1f

              if (isMultiTouch || isZoomed) {
                // Apply transformations
                val newScale = (scale * zoomChange).coerceIn(1f, 5f)
                scale = newScale

                val maxOffsetX = (size.width * (scale - 1)) / 2
                val maxOffsetY = (size.height * (scale - 1)) / 2

                val newX = (offset.x + panChange.x * scale).coerceIn(-maxOffsetX, maxOffsetX)
                val newY = (offset.y + panChange.y * scale).coerceIn(-maxOffsetY, maxOffsetY)

                offset = Offset(newX, newY)

                // Consume the changes to prevent parent Pager from intercepting
                event.changes.forEach {
                  if (it.positionChanged()) {
                    it.consume()
                  }
                }
              }
              // Else: Single touch AND scale == 1.0 -> Do NOT consume.
              // Parent HorizontalPager will see the unconsumed event and handle the scroll.
            }
          } while (event.changes.any { it.pressed })
        }
      }
  ) {
    AsyncImage(
      model = file,
      contentDescription = "Full Image",
      modifier = Modifier
        .align(Alignment.Center)
        .graphicsLayer {
          scaleX = scale
          scaleY = scale
          translationX = offset.x
          translationY = offset.y
        },
      contentScale = ContentScale.Fit
    )
  }
}

@OptIn(UnstableApi::class)
@Composable
fun VideoPlayer(file: File) {
  val context = LocalContext.current

  val exoPlayer = remember {
    ExoPlayer.Builder(context).build()
  }

  DisposableEffect(file) {
    val mediaItem = MediaItem.fromUri(Uri.fromFile(file))
    exoPlayer.setMediaItem(mediaItem)
    exoPlayer.prepare()
    exoPlayer.playWhenReady = true

    onDispose {
      exoPlayer.release()
    }
  }

  AndroidView(
    factory = { ctx ->
      PlayerView(ctx).apply {
        player = exoPlayer
        layoutParams = ViewGroup.LayoutParams(
          ViewGroup.LayoutParams.MATCH_PARENT,
          ViewGroup.LayoutParams.MATCH_PARENT
        )
        useController = true
        setShowBuffering(PlayerView.SHOW_BUFFERING_ALWAYS)
      }
    },
    modifier = Modifier.fillMaxSize()
  )
}
