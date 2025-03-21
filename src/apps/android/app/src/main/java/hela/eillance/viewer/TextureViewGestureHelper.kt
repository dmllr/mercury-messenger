package hela.eillance.viewer

import android.annotation.SuppressLint
import android.graphics.Matrix
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.TextureView
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import kotlin.math.max
import kotlin.math.min

// Extension function to add pinch-to-zoom to TextureView
fun TextureView.setupPinchToZoom(
  calculateInitialMatrix: () -> Matrix,
  onScaleChange: ((Float) -> Unit)? = null
) {
  // Current scale factor - starts at 1.0f (original size)
  var scaleFactor = 1.0f

  // Maximum scale is 4.0f (400%)
  val maxScale = 4.0f

  // Create scale gesture detector
  val scaleGestureDetector = ScaleGestureDetector(
    context,
    object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
      override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
        return true
      }

      override fun onScale(detector: ScaleGestureDetector): Boolean {
        // Calculate new scale factor, constrained between 1.0 and maxScale
        scaleFactor = max(1.0f, min(scaleFactor * detector.scaleFactor, maxScale))
        // Apply transformations
        applyTransformations(calculateInitialMatrix(), scaleFactor, detector.focusX, detector.focusY)
        // Notify scale change if callback is provided
        onScaleChange?.invoke(scaleFactor)

        return true
      }
    }
  )

  // Listen for touch events
  setOnTouchListener { view, event ->
    // Forward events to the scale gesture detector
    val consumed = scaleGestureDetector.onTouchEvent(event)

    // Return true if we're scaling, otherwise let other touch handlers process it
    when (event.action) {
      MotionEvent.ACTION_DOWN -> true
      MotionEvent.ACTION_UP -> {
        // If scale is very close to 1.0, snap back to exact fit
        if (scaleFactor < 1.05f) {
          scaleFactor = 1.0f
          setTransform(calculateInitialMatrix())
          onScaleChange?.invoke(1.0f)
        }

        // If this was a tap (not a scale), call performClick
        if (!consumed && event.pointerCount == 1) {
          view.performClick()
        }

        false
      }
      else -> consumed
    }
  }

  // To satisfy linter
  setOnClickListener {}

  // Initial transform
  setTransform(calculateInitialMatrix())
}

// Helper method to apply transformations based on pinch gesture
private fun TextureView.applyTransformations(
  baseMatrix: Matrix,
  scaleFactor: Float,
  focusX: Float,
  focusY: Float
) {
  val matrix = Matrix(baseMatrix)

  if (scaleFactor > 1.0f) {
    matrix.postScale(scaleFactor, scaleFactor, focusX, focusY)
  }

  setTransform(matrix)
}

// Composable modifier to add pinch-to-zoom functionality
fun Modifier.pinchToZoom(
  textureView: TextureView?,
  calculateBaseMatrix: () -> Matrix,
  onScaleChange: (Float) -> Unit = {}
): Modifier = this.then(
  Modifier.pointerInput(Unit) {
    var currentScale = 1f
    detectTransformGestures { centroid, pan, zoom, _ ->
      currentScale = (currentScale * zoom).coerceIn(1f, 4f)

      textureView?.let {
        val matrix = calculateBaseMatrix()

        if (currentScale > 1.0f) {
          matrix.postScale(
            currentScale,
            currentScale,
            centroid.x,
            centroid.y
          )
        }

        it.setTransform(matrix)
        onScaleChange(currentScale)
      }
    }
  }
)

// Compose-friendly wrapper for the TextureView scaling
@SuppressLint("ClickableViewAccessibility")
@Composable
fun rememberTextureViewScaling(
  textureView: TextureView?,
  calculateBaseMatrix: () -> Matrix
): Triple<Float, (Float) -> Unit, Modifier> {
  var currentScale by remember { mutableFloatStateOf(1f) }
  val updateScale: (Float) -> Unit = { newScale ->
    currentScale = newScale
  }

  val gestureModifier = Modifier.pinchToZoom(
    textureView = textureView,
    calculateBaseMatrix = calculateBaseMatrix,
    onScaleChange = updateScale
  )

  // Set up scaling on the TextureView
  DisposableEffect(textureView) {
    textureView?.setupPinchToZoom(
      calculateInitialMatrix = calculateBaseMatrix,
      onScaleChange = updateScale
    )

    onDispose {
      textureView?.let {
        it.setOnTouchListener(null)
        it.setOnClickListener(null)
      }
    }
  }

  return Triple(currentScale, updateScale, gestureModifier)
}
