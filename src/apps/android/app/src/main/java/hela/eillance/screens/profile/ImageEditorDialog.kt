package hela.eillance.screens.profile

import android.graphics.Bitmap
import android.graphics.Matrix
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.RotateLeft
import androidx.compose.material.icons.automirrored.filled.RotateRight
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathOperation
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.graphics.createBitmap
import hela.eillance.camera.R
import kotlin.math.min

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ImageEditorDialog(
  bitmap: Bitmap,
  onDismiss: () -> Unit,
  onSave: (Bitmap) -> Unit
) {
  var scale by remember { mutableFloatStateOf(1f) }
  var offset by remember { mutableStateOf(Offset.Zero) }
  var rotation by remember { mutableFloatStateOf(0f) }

  Dialog(
    onDismissRequest = onDismiss,
    properties = DialogProperties(usePlatformDefaultWidth = false)
  ) {
    Scaffold(
      topBar = {
        TopAppBar(
          title = {
            Text(
              stringResource(id = R.string.photo_editor_title),
              color = Color.White
            )
          },
          navigationIcon = {
            IconButton(onClick = onDismiss) {
              Icon(
                Icons.Default.Close,
                contentDescription = stringResource(id = R.string.action_close),
                tint = Color.White
              )
            }
          },
          actions = {},
          colors = TopAppBarDefaults.topAppBarColors(
            containerColor = Color.Black
          )
        )
      },
      containerColor = Color.Black
    ) { padding ->
      BoxWithConstraints(
        modifier = Modifier
          .fillMaxSize()
          .padding(padding)
          .background(Color.Black),
        contentAlignment = Alignment.Center
      ) {
        val screenWidth = constraints.maxWidth.toFloat()
        val screenHeight = constraints.maxHeight.toFloat()
        val maskDiameter = min(screenWidth, screenHeight) * 0.9f
        val maskRadius = maskDiameter / 2f
        val maskRect = Rect(
          offset = Offset(
            (screenWidth - maskDiameter) / 2f,
            (screenHeight - maskDiameter) / 2f
          ),
          size = Size(maskDiameter, maskDiameter)
        )
        val center = Offset(screenWidth / 2f, screenHeight / 2f)

        val imageBitmap = remember(bitmap) { bitmap.asImageBitmap() }

        // Gestures and Canvas rendering
        Box(
          modifier = Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
              detectTransformGestures { centroid, pan, zoom, _ ->
                val oldScale = scale
                val newScale = (scale * zoom).coerceIn(0.5f, 5f)

                // Adjust offset to pivot the zoom around the gesture centroid.
                val v = centroid - center
                val newOffset = v - (v - offset) * (newScale / oldScale) + pan

                scale = newScale
                offset = newOffset
              }
            }
        ) {
          Canvas(modifier = Modifier.fillMaxSize()) {
            withTransform({
              translate(left = center.x + offset.x, top = center.y + offset.y)
              scale(scale, scale, pivot = Offset.Zero)
              rotate(rotation, pivot = Offset.Zero)
              translate(left = -imageBitmap.width / 2f, top = -imageBitmap.height / 2f)
            }) {
              drawImage(imageBitmap)
            }

            val path = Path().apply {
              addRect(Rect(0f, 0f, size.width, size.height))
              val circlePath = Path().apply { addOval(maskRect) }
              op(this, circlePath, PathOperation.Difference)
            }

            drawPath(path, Color.Black.copy(alpha = 0.7f))

            drawCircle(
              color = Color.White,
              radius = maskRadius,
              center = maskRect.center,
              style = Stroke(width = 2.dp.toPx())
            )
          }

          // Floating Save Button
          Button(
            onClick = {
              val cropped = performCrop(bitmap, scale, offset, rotation, maskDiameter)
              onSave(cropped)
            },
            colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.primary),
            modifier = Modifier
              .align(Alignment.TopEnd)
              .padding(16.dp)
          ) {
            Icon(
              Icons.Default.Check,
              contentDescription = stringResource(id = R.string.action_save),
              tint = MaterialTheme.colorScheme.onPrimary
            )
            Text(stringResource(id = R.string.action_apply))
          }

          // Rotation controls at the bottom
          Row(
            modifier = Modifier
              .align(Alignment.BottomCenter)
              .padding(bottom = 32.dp),
            horizontalArrangement = Arrangement.spacedBy(32.dp)
          ) {
            IconButton(
              onClick = { rotation -= 90f },
              modifier = Modifier
                .size(56.dp)
                .background(Color.DarkGray.copy(alpha = 0.8f), CircleShape)
            ) {
              Icon(
                Icons.AutoMirrored.Filled.RotateLeft,
                contentDescription = stringResource(id = R.string.action_rotate_left),
                tint = Color.White
              )
            }
            IconButton(
              onClick = { rotation += 90f },
              modifier = Modifier
                .size(56.dp)
                .background(Color.DarkGray.copy(alpha = 0.8f), CircleShape)
            ) {
              Icon(
                Icons.AutoMirrored.Filled.RotateRight,
                contentDescription = stringResource(id = R.string.action_rotate_right),
                tint = Color.White
              )
            }
          }
        }
      }
    }
  }
}

private fun performCrop(
  original: Bitmap,
  visualScale: Float,
  visualOffset: Offset,
  visualRotation: Float,
  visualMaskDiameter: Float,
  targetSize: Int = 512
): Bitmap {
  val output = createBitmap(targetSize, targetSize)

  val canvas = android.graphics.Canvas(output)
  val matrix = Matrix()

  matrix.postTranslate(-original.width / 2f, -original.height / 2f)
  matrix.postRotate(visualRotation)

  val ratio = targetSize.toFloat() / visualMaskDiameter
  val finalScale = visualScale * ratio
  matrix.postScale(finalScale, finalScale)

  matrix.postTranslate(
    (targetSize / 2f) + (visualOffset.x * ratio),
    (targetSize / 2f) + (visualOffset.y * ratio)
  )

  val paint = android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG)
  canvas.drawBitmap(original, matrix, paint)

  return output
}
