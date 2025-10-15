package hela.eillance.ui.call

import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import hela.eillance.MainViewModel

@Composable
fun CallScreen(
  peerId: String,
  viewModel: MainViewModel,
  onHangup: () -> Unit,
  initDecoder: (Surface) -> Unit,
  releaseDecoder: () -> Unit
) {
  Box(modifier = Modifier.fillMaxSize()) {
    AndroidView(
      factory = { context ->
        TextureView(context).apply {
          surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
              initDecoder(Surface(surface))
            }
            override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {}
            override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
              releaseDecoder()
              return true
            }
            override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {}
          }
        }
      },
      modifier = Modifier.fillMaxSize()
    )

    Button(
      onClick = onHangup,
      modifier = Modifier.align(Alignment.BottomCenter)
    ) {
      Text("Hang Up")
    }
  }

  DisposableEffect(key1 = peerId) {
    onDispose {
      releaseDecoder()
    }
  }
}
