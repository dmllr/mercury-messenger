package hela.eillance.screens

import android.Manifest
import android.content.pm.PackageManager
import android.util.Log
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.MultiFormatReader
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer
import hela.eillance.camera.R
import java.nio.ByteBuffer
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

@Composable
fun QrScannerScreen(
  onQrCodeScanned: (peerId: String, token: String?) -> Unit,
  onClose: () -> Unit
) {
  val context = LocalContext.current
  val lifecycleOwner = androidx.lifecycle.compose.LocalLifecycleOwner.current
  var hasCameraPermission by remember {
    mutableStateOf(
      ContextCompat.checkSelfPermission(
        context,
        Manifest.permission.CAMERA
      ) == PackageManager.PERMISSION_GRANTED
    )
  }
  val launcher = rememberLauncherForActivityResult(
    contract = ActivityResultContracts.RequestPermission(),
    onResult = { granted ->
      hasCameraPermission = granted
    }
  )

  LaunchedEffect(Unit) {
    if (!hasCameraPermission) {
      launcher.launch(Manifest.permission.CAMERA)
    }
  }

  Box(modifier = Modifier.fillMaxSize()) {
    if (hasCameraPermission) {
      val cameraProviderFuture = remember { ProcessCameraProvider.getInstance(context) }
      val cameraExecutor = remember { Executors.newSingleThreadExecutor() }
      val qrCodeScanned = remember { AtomicBoolean(false) }

      AndroidView(
        factory = { ctx ->
          val previewView = PreviewView(ctx).apply {
            layoutParams = ViewGroup.LayoutParams(
              ViewGroup.LayoutParams.MATCH_PARENT,
              ViewGroup.LayoutParams.MATCH_PARENT
            )
          }

          cameraProviderFuture.addListener({
            val cameraProvider = cameraProviderFuture.get()

            val preview = Preview.Builder().build().also {
              it.surfaceProvider = previewView.surfaceProvider
            }

            // Using ZXing MultiFormatReader
            val reader = MultiFormatReader().apply {
              setHints(mapOf(DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE)))
            }

            val imageAnalyzer = ImageAnalysis.Builder()
              .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
              .build()
              .also {
                it.setAnalyzer(cameraExecutor) { imageProxy ->
                  if (qrCodeScanned.get()) {
                    imageProxy.close()
                    return@setAnalyzer
                  }

                  try {
                    val rawResult = scanImage(reader, imageProxy)
                    if (rawResult != null) {
                      val rawValue = rawResult.text
                      val prefix = "mercury:contact:"
                      if (rawValue.startsWith(prefix) && qrCodeScanned.compareAndSet(false, true)) {
                        val fullData = rawValue.substring(prefix.length)
                        val parts = fullData.split('?', limit = 2)
                        val peerId = parts[0]
                        var token: String? = null

                        if (parts.size > 1) {
                          val query = parts[1]
                          val queryParams = query.split('&')
                          for (param in queryParams) {
                            val keyValue = param.split('=', limit = 2)
                            if (keyValue.size == 2 && keyValue[0] == "token") {
                              token = keyValue[1]
                              break
                            }
                          }
                        }

                        if (peerId.isNotBlank()) {
                          // Switch to main thread for callback
                          previewView.post {
                            onQrCodeScanned(peerId, token)
                          }
                        } else {
                          // Reset if invalid
                          qrCodeScanned.set(false)
                        }
                      }
                    }
                  } catch (e: Exception) {
                    Log.e("QrScannerScreen", "Analysis failed", e)
                  } finally {
                    imageProxy.close()
                  }
                }
              }

            try {
              cameraProvider.unbindAll()
              cameraProvider.bindToLifecycle(
                lifecycleOwner,
                CameraSelector.DEFAULT_BACK_CAMERA,
                preview,
                imageAnalyzer
              )
            } catch (e: Exception) {
              Toast.makeText(
                context,
                context.getString(R.string.qr_camera_fail, e.message),
                Toast.LENGTH_LONG
              ).show()
            }
          }, ContextCompat.getMainExecutor(ctx))
          previewView
        },
        modifier = Modifier.fillMaxSize(),
        onRelease = {
          cameraExecutor.shutdown()
        }
      )
    } else {
      Text(
        stringResource(id = R.string.qr_permission_required),
        modifier = Modifier.align(Alignment.Center)
      )
    }

    IconButton(
      onClick = onClose,
      modifier = Modifier
        .statusBarsPadding()
        .align(Alignment.TopStart)
        .padding(16.dp)
    ) {
      Icon(Icons.Default.Close, contentDescription = stringResource(id = R.string.qr_action_close))
    }
  }
}

private fun scanImage(reader: MultiFormatReader, image: ImageProxy): com.google.zxing.Result? {
  val buffer: ByteBuffer = image.planes[0].buffer
  val width = image.width
  val height = image.height
  val rowStride = image.planes[0].rowStride
  val pixelStride = image.planes[0].pixelStride

  // Check if buffer is valid YUV (Y-plane)
  if (pixelStride != 1)
    return null

  // If stride matches width, we can use the buffer directly
  val data = if (rowStride == width) {
    val bytes = ByteArray(buffer.remaining())
    buffer.get(bytes)
    bytes
  } else {
    // If there is padding, we must remove it
    val bytes = ByteArray(width * height)
    for (y in 0 until height) {
      buffer.position(y * rowStride)
      buffer.get(bytes, y * width, width)
    }
    bytes
  }

  // QR Codes are omni-directional, passing the raw Y-plane data is sufficient and performant.
  val source = PlanarYUVLuminanceSource(
    data,
    width,
    height,
    0, 0,
    width,
    height,
    false
  )

  val bitmap = BinaryBitmap(HybridBinarizer(source))
  return try {
    reader.decodeWithState(bitmap)
  } catch (e: Exception) {
    null
  } finally {
    reader.reset()
  }
}
