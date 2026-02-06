package hela.eillance.screens.conversation

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TextFieldDefaults
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import coil.compose.AsyncImage
import coil.decode.VideoFrameDecoder
import coil.request.ImageRequest
import coil.request.videoFrameMillis
import hela.eillance.camera.R
import hela.eillance.viewmodels.DraftAttachment

@OptIn(ExperimentalFoundationApi::class, ExperimentalMaterial3Api::class)
@Composable
fun DraftAttachmentEditor(
  drafts: List<DraftAttachment>,
  initialIndex: Int,
  onDismiss: () -> Unit,
  onRemove: (Int) -> Unit,
  onNoteChange: (Int, String) -> Unit
) {
  val pagerState = rememberPagerState(initialPage = initialIndex, pageCount = { drafts.size })
  val context = LocalContext.current

  Dialog(
    onDismissRequest = onDismiss,
    properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)
  ) {
    Scaffold(
      containerColor = Color.Black,
      contentWindowInsets = WindowInsets.systemBars.union(WindowInsets.ime),
      topBar = {
        TopAppBar(
          title = {
            Text(
              stringResource(
                id = R.string.attachment_editor_pager_indicator,
                pagerState.currentPage + 1,
                drafts.size
              ), color = Color.White
            )
          },
          navigationIcon = {
            IconButton(onClick = onDismiss) {
              Icon(
                Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = stringResource(id = R.string.action_back),
                tint = Color.White
              )
            }
          },
          actions = {
            IconButton(onClick = { onRemove(pagerState.currentPage) }) {
              Icon(
                Icons.Default.Delete,
                contentDescription = stringResource(id = R.string.action_delete),
                tint = Color.White
              )
            }
          },
          colors = TopAppBarDefaults.topAppBarColors(containerColor = Color.Black.copy(alpha = 0.5f))
        )
      }
    ) { padding ->
      Box(
        modifier = Modifier
          .fillMaxSize()
          .padding(padding)
      ) {
        HorizontalPager(
          state = pagerState,
          modifier = Modifier.fillMaxSize()
        ) { page ->
          val draft = drafts[page]
          Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            val isVideo = draft.mimeType.startsWith("video/")
            val isImage = draft.mimeType.startsWith("image/")

            if (isImage || isVideo) {
              val model = if (isVideo) {
                ImageRequest.Builder(context)
                  .data(draft.uri)
                  .decoderFactory { result, options, _ -> VideoFrameDecoder(result.source, options) }
                  .videoFrameMillis(1000)
                  .build()
              } else draft.uri

              AsyncImage(
                model = model,
                contentDescription = null,
                contentScale = ContentScale.Fit,
                modifier = Modifier.fillMaxSize()
              )
            } else {
              Icon(
                imageVector = getDraftIcon(draft.mimeType),
                contentDescription = null,
                tint = Color.Gray,
                modifier = Modifier.size(120.dp)
              )
            }
          }
        }

        Box(
          modifier = Modifier
            .align(Alignment.BottomCenter)
            .fillMaxWidth()
            .background(
              Brush.verticalGradient(
                colors = listOf(Color.Transparent, Color.Black.copy(alpha = 0.9f))
              )
            )
            .padding(16.dp)
            .padding(bottom = 16.dp)
        ) {
          TextField(
            value = drafts[pagerState.currentPage].note,
            onValueChange = { onNoteChange(pagerState.currentPage, it) },
            placeholder = {
              Text(
                stringResource(R.string.attachment_note_placeholder),
                color = Color.LightGray
              )
            },
            colors = TextFieldDefaults.colors(
              focusedContainerColor = Color.Transparent,
              unfocusedContainerColor = Color.Transparent,
              focusedTextColor = Color.White,
              unfocusedTextColor = Color.White,
              cursorColor = MaterialTheme.colorScheme.primary,
              focusedIndicatorColor = Color.White.copy(alpha = 0.5f),
              unfocusedIndicatorColor = Color.White.copy(alpha = 0.3f)
            ),
            modifier = Modifier.fillMaxWidth(),
            maxLines = 4
          )
        }
      }
    }
  }
}
