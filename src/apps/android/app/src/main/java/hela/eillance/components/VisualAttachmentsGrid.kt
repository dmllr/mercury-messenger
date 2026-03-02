package hela.eillance.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import hela.eillance.camera.R
import hela.eillance.ndkio.Attachment

@Composable
fun VisualAttachmentsGrid(
  attachments: List<Attachment>,
  transferProgress: Map<String, Float>,
  isFromMe: Boolean,
  selectionMode: Boolean,
  onMediaClick: (Attachment) -> Unit,
  onLongClick: (() -> Unit)? = null,
  onCancel: (String) -> Unit,
  onResume: (String) -> Unit,
  onDiscard: (String) -> Unit
) {
  val count = attachments.size
  val roundedShape = RoundedCornerShape(8.dp)

  val renderItem: @Composable (Int, Modifier) -> Unit = { index, modifier ->
    val att = attachments[index]
    MediaItem(
      attachment = att,
      progress = transferProgress[att.id],
      isFromMe = isFromMe,
      selectionMode = selectionMode,
      onClick = { onMediaClick(att) },
      onLongClick = onLongClick,
      onCancel = { onCancel(att.id) },
      onResume = { onResume(att.id) },
      onDiscard = { onDiscard(att.id) },
      modifier = modifier
    )
  }

  Column(
    modifier = Modifier
      .fillMaxWidth()
      .clip(roundedShape)
  ) {
    when (count) {
      1 -> renderItem(
        0, Modifier
          .fillMaxWidth()
          .aspectRatio(1.5f)
      )

      2 -> {
        Row(
          modifier = Modifier
            .fillMaxWidth()
            .height(160.dp)
        ) {
          renderItem(
            0, Modifier
              .weight(1f)
              .fillMaxSize()
          )
          Spacer(modifier = Modifier.width(2.dp))
          renderItem(
            1, Modifier
              .weight(1f)
              .fillMaxSize()
          )
        }
      }

      3 -> {
        Column(modifier = Modifier.fillMaxWidth()) {
          renderItem(
            0, Modifier
              .fillMaxWidth()
              .height(160.dp)
          )
          Spacer(modifier = Modifier.height(2.dp))
          Row(
            modifier = Modifier
              .fillMaxWidth()
              .height(100.dp)
          ) {
            renderItem(
              1, Modifier
                .weight(1f)
                .fillMaxSize()
            )
            Spacer(modifier = Modifier.width(2.dp))
            renderItem(
              2, Modifier
                .weight(1f)
                .fillMaxSize()
            )
          }
        }
      }

      else -> {
        Column(modifier = Modifier.fillMaxWidth()) {
          Row(
            modifier = Modifier
              .fillMaxWidth()
              .height(120.dp)
          ) {
            renderItem(
              0, Modifier
                .weight(1f)
                .fillMaxSize()
            )
            Spacer(modifier = Modifier.width(2.dp))
            renderItem(
              1, Modifier
                .weight(1f)
                .fillMaxSize()
            )
          }
          Spacer(modifier = Modifier.height(2.dp))
          Row(
            modifier = Modifier
              .fillMaxWidth()
              .height(120.dp)
          ) {
            renderItem(
              2, Modifier
                .weight(1f)
                .fillMaxSize()
            )
            Spacer(modifier = Modifier.width(2.dp))
            Box(
              modifier = Modifier
                .weight(1f)
                .fillMaxSize()
            ) {
              renderItem(3, Modifier.fillMaxSize())
              if (count > 4) {
                Box(
                  modifier = Modifier
                    .fillMaxSize()
                    .background(Color.Black.copy(alpha = 0.5f))
                    .clickable(enabled = !selectionMode) { onMediaClick(attachments[3]) },
                  contentAlignment = Alignment.Center
                ) {
                  Text(
                    text = stringResource(id = R.string.plus_more, count - 4),
                    style = MaterialTheme.typography.titleLarge,
                    color = Color.White
                  )
                }
              }
            }
          }
        }
      }
    }
  }
}
