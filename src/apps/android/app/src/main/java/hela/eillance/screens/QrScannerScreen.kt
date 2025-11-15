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
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import com.google.mlkit.vision.barcode.BarcodeScannerOptions
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean

@Composable
fun QrScannerScreen(
  onQrCodeScanned: (String) -> Unit,
  onClose: () -> Unit
) {
  val context = LocalContext.current
  val lifecycleOwner = androidx.lifecycle.compose.LocalLifecycleOwner.current
  var hasCameraPermission by remember {
    mutableStateOf(ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED)
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
              it.setSurfaceProvider(previewView.surfaceProvider)
            }

            val options = BarcodeScannerOptions.Builder()
              .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
              .build()
            val scanner = BarcodeScanning.getClient(options)

            val imageAnalyzer = ImageAnalysis.Builder()
              .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
              .build()
              .also {
                it.setAnalyzer(cameraExecutor) { imageProxy ->
                  val mediaImage = imageProxy.image
                  if (mediaImage != null) {
                    val image = InputImage.fromMediaImage(mediaImage, imageProxy.imageInfo.rotationDegrees)
                    scanner.process(image)
                      .addOnSuccessListener { barcodes ->
                        for (barcode in barcodes) {
                          barcode.rawValue?.let { rawValue ->
                            val prefix = "mercury:contact:"
                            if (rawValue.startsWith(prefix) && qrCodeScanned.compareAndSet(false, true)) {
                              val peerId = rawValue.substring(prefix.length)
                              if (peerId.isNotBlank()) {
                                onQrCodeScanned(peerId)
                              }
                            }
                          }
                        }
                      }
                      .addOnFailureListener { e ->
                        Log.e("QrScannerScreen", "Barcode scanning failed", e)
                      }
                      .addOnCompleteListener {
                        imageProxy.close()
                      }
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
              Toast.makeText(context, "Failed to start camera: ${e.message}", Toast.LENGTH_LONG).show()
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
      Text("Camera permission is required to scan QR codes.", modifier = Modifier.align(Alignment.Center))
    }

    IconButton(
      onClick = onClose,
      modifier = Modifier.align(Alignment.TopStart).padding(16.dp)
    ) {
      Icon(Icons.Default.Close, contentDescription = "Close Scanner")
    }
  }
}
