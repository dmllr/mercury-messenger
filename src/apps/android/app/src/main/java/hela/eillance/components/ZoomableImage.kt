package hela.eillance.components

import androidx.compose.foundation.Image
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.IntSize
import coil.compose.AsyncImage
import hela.eillance.camera.R
import hela.eillance.utils.BitmapUtils
import java.io.File

@Composable
fun ZoomableImage(file: File) {
  var scale by remember { mutableFloatStateOf(1f) }
  var offset by remember { mutableStateOf(Offset.Zero) }
  var size by remember { mutableStateOf(IntSize.Zero) }

  val isGif = file.extension.lowercase() == "gif"
  val gifBitmap = remember(file.absolutePath) {
    if (isGif) {
      val options = android.graphics.BitmapFactory.Options()
      options.inJustDecodeBounds = true
      android.graphics.BitmapFactory.decodeFile(file.absolutePath, options)
      options.inSampleSize = BitmapUtils.calculateInSampleSize(options, 2048, 2048)
      options.inJustDecodeBounds = false
      android.graphics.BitmapFactory.decodeFile(file.absolutePath, options)?.asImageBitmap()
    } else null
  }

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

              val isMultiTouch = event.changes.size > 1
              val isZoomed = scale > 1f

              if (isMultiTouch || isZoomed) {
                val newScale = (scale * zoomChange).coerceIn(1f, 5f)
                scale = newScale

                val maxOffsetX = (size.width * (scale - 1)) / 2
                val maxOffsetY = (size.height * (scale - 1)) / 2

                val newX = (offset.x + panChange.x * scale).coerceIn(-maxOffsetX, maxOffsetX)
                val newY = (offset.y + panChange.y * scale).coerceIn(-maxOffsetY, maxOffsetY)

                offset = Offset(newX, newY)

                event.changes.forEach {
                  if (it.positionChanged()) {
                    it.consume()
                  }
                }
              }
            }
          } while (event.changes.any { it.pressed })
        }
      }
  ) {
    val imageModifier = Modifier
      .align(Alignment.Center)
      .graphicsLayer {
        scaleX = scale
        scaleY = scale
        translationX = offset.x
        translationY = offset.y
      }

    if (gifBitmap != null) {
      Image(
        bitmap = gifBitmap,
        contentDescription = stringResource(id = R.string.media_preview_full_image_description),
        modifier = imageModifier,
        contentScale = ContentScale.Fit
      )
    } else {
      AsyncImage(
        model = file,
        contentDescription = stringResource(id = R.string.media_preview_full_image_description),
        modifier = imageModifier,
        contentScale = ContentScale.Fit
      )
    }
  }
}
