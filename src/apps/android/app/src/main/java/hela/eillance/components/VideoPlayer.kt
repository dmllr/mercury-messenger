package hela.eillance.components

import android.net.Uri
import android.view.ViewGroup
import androidx.annotation.OptIn
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.viewinterop.AndroidView
import androidx.media3.common.MediaItem
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.ui.PlayerView
import java.io.File

@OptIn(UnstableApi::class)
@Composable
fun VideoPlayer(file: File, isPlaying: Boolean) {
  val context = LocalContext.current

  val exoPlayer = remember {
    ExoPlayer.Builder(context).build()
  }

  DisposableEffect(file) {
    val mediaItem = MediaItem.fromUri(Uri.fromFile(file))
    exoPlayer.setMediaItem(mediaItem)
    exoPlayer.prepare()

    onDispose {
      exoPlayer.release()
    }
  }

  LaunchedEffect(isPlaying) {
    exoPlayer.playWhenReady = isPlaying
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
