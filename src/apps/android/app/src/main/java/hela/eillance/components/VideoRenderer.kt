package hela.eillance.components

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView
import org.webrtc.EglBase
import org.webrtc.SurfaceViewRenderer
import org.webrtc.VideoSink

/**
 * A Composable wrapper around WebRTC's SurfaceViewRenderer.
 * Handles EGL context initialization and view lifecycle.
 */
@Composable
fun VideoRenderer(
  eglBaseContext: EglBase.Context,
  modifier: Modifier = Modifier,
  isMirror: Boolean = false,
  zIndex: Boolean = false,
  onUpdate: (VideoSink) -> Unit,
  onRelease: () -> Unit
) {
  AndroidView(
    factory = { context ->
      SurfaceViewRenderer(context).apply {
        init(eglBaseContext, null)
        setMirror(isMirror)
        setEnableHardwareScaler(true)
        if (zIndex) {
          setZOrderMediaOverlay(true)
        }
      }
    },
    modifier = modifier,
    update = { renderer ->
      onUpdate(renderer)
    },
    onRelease = { renderer ->
      onRelease()
      renderer.release()
    }
  )
}
