package hela.eillance.screens

import android.view.ViewGroup
import android.widget.VideoView
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import coil.compose.AsyncImage
import hela.eillance.ndkio.AttachmentType
import java.io.File

@Composable
fun FullScreenMediaScreen(path: String, type: AttachmentType, onBack: () -> Unit) {
  val file = File(path)

  Box(
    modifier = Modifier
      .fillMaxSize()
      .background(Color.Black)
  ) {
    if (!file.exists()) {
      CircularProgressIndicator(modifier = Modifier.align(Alignment.Center))
    } else {
      if (type == AttachmentType.Video) {
        VideoPlayer(file)
      } else {
        ZoomableImage(file)
      }
    }

    IconButton(
      onClick = onBack,
      modifier = Modifier
        .align(Alignment.TopStart)
        .padding(16.dp)
    ) {
      Icon(Icons.Default.Close, contentDescription = "Close", tint = Color.White)
    }
  }
}

@Composable
fun ZoomableImage(file: File) {
  var scale by remember { mutableStateOf(1f) }
  var offsetX by remember { mutableStateOf(0f) }
  var offsetY by remember { mutableStateOf(0f) }

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

@Composable
fun VideoPlayer(file: File) {
  AndroidView(
    factory = { context ->
      VideoView(context).apply {
        layoutParams = ViewGroup.LayoutParams(
          ViewGroup.LayoutParams.MATCH_PARENT,
          ViewGroup.LayoutParams.MATCH_PARENT
        )
        setVideoPath(file.absolutePath)
        start()
        setOnCompletionListener { start() } // Loop video
      }
    },
    modifier = Modifier.fillMaxSize()
  )
}
