package hela.eillance.screens

import android.net.Uri
import android.view.ViewGroup
import androidx.annotation.OptIn
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import coil.compose.AsyncImage
import hela.eillance.ndkio.AttachmentType
import java.io.File
import java.util.Locale

@kotlin.OptIn(ExperimentalMaterial3Api::class)
@Composable
fun FullScreenMediaScreen(path: String, type: AttachmentType, mimeType: String, onBack: () -> Unit) {
  val file = File(path)

  Scaffold(
    containerColor = Color.Black,
    topBar = {
      TopAppBar(
        title = { }, // No title for immersive feel, or could add file name
        navigationIcon = {
          IconButton(onClick = onBack) {
            Icon(
              imageVector = Icons.AutoMirrored.Filled.ArrowBack,
              contentDescription = "Back"
            )
          }
        },
        colors = TopAppBarDefaults.topAppBarColors(
          containerColor = Color.Black,
          navigationIconContentColor = Color.White,
          actionIconContentColor = Color.White
        )
      )
    }
  ) { padding ->
    Box(
      modifier = Modifier
        .fillMaxSize()
        .padding(padding)
        .background(Color.Black)
    ) {
      if (!file.exists()) {
        CircularProgressIndicator(modifier = Modifier.align(Alignment.Center), color = Color.White)
      } else {
        // Robust video check: explicit Type OR Mime prefix OR file extension fallback
        val isVideo = type == AttachmentType.Video ||
            mimeType.startsWith("video/") ||
            file.extension.lowercase(Locale.ROOT) in setOf("mp4", "mkv", "webm", "avi", "mov", "3gp")

        if (isVideo) {
          VideoPlayer(file)
        } else {
          ZoomableImage(file)
        }
      }
    }
  }
}

@Composable
fun ZoomableImage(file: File) {
  var scale by remember { mutableFloatStateOf(1f) }
  var offsetX by remember { mutableFloatStateOf(0f) }
  var offsetY by remember { mutableFloatStateOf(0f) }

  Box(
    modifier = Modifier
      .fillMaxSize()
      .pointerInput(Unit) {
        detectTransformGestures { _, pan, zoom, _ ->
          scale = maxOf(1f, minOf(scale * zoom, 5f))
          if (scale > 1f) {
            val maxOffsetX = (size.width * (scale - 1)) / 2
            val maxOffsetY = (size.height * (scale - 1)) / 2
            offsetX = maxOf(-maxOffsetX, minOf(maxOffsetX, offsetX + pan.x))
            offsetY = maxOf(-maxOffsetY, minOf(maxOffsetY, offsetY + pan.y))
          } else {
            offsetX = 0f
            offsetY = 0f
          }
        }
      }
  ) {
    AsyncImage(
      model = file,
      contentDescription = "Full Image",
      modifier = Modifier
        .align(Alignment.Center)
        .graphicsLayer(
          scaleX = scale,
          scaleY = scale,
          translationX = offsetX,
          translationY = offsetY
        ),
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
