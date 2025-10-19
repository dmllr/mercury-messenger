package hela.eillance

import android.graphics.SurfaceTexture
import android.view.Surface
import android.view.TextureView
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CallEnd
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView

@Composable
fun CallScreen(
  peerId: String,
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

    FloatingActionButton(
      onClick = onHangup,
      modifier = Modifier
        .align(Alignment.BottomCenter)
        .padding(16.dp)
    ) {
      Icon(Icons.Default.CallEnd, contentDescription = "Hang Up")
    }
  }

  // Ensure decoder is released when this screen is disposed
  DisposableEffect(key1 = peerId) {
    onDispose {
      releaseDecoder()
    }
  }
}
