package hela.eillance.components

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.util.Base64
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.Image
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Group
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import hela.eillance.ndkio.Contact
import hela.eillance.viewmodels.ContactsViewModel
import kotlin.math.cos
import kotlin.math.sin

data class MemberInfo(val peerId: String, val name: String, val photoBase64: String?)

private val groupAvatarCache = android.util.LruCache<String, androidx.compose.ui.graphics.ImageBitmap>(50)
private const val MAX_GROUP_AVATARS = 4

@Composable
fun ContactAvatar(
  contact: Contact,
  contactsVM: ContactsViewModel,
  modifier: Modifier = Modifier,
  onClick: (() -> Unit)? = null
) {
  if (contact.isGroup && contact.photo.isNullOrEmpty()) {
    val contactsList by contactsVM.contacts.collectAsState()
    val members = remember(contact.peerId) { contactsVM.getGroupMembers(contact.peerId) }

    val memberInfos = remember(members, contactsList) {
      members.map { m ->
        val c = contactsList.find { it.peerId == m.peerId }
        MemberInfo(m.peerId, c?.displayName ?: m.peerId.take(8), c?.photo)
      }
    }

    GroupAvatarCached(
      groupId = contact.peerId,
      members = memberInfos,
      modifier = modifier,
      onClick = onClick
    )
  } else {
    UserAvatar(
      photoBase64 = contact.photo,
      modifier = modifier,
      onClick = onClick
    )
  }
}

@Composable
fun GroupAvatarCached(
  groupId: String,
  members: List<MemberInfo>,
  modifier: Modifier = Modifier,
  onClick: (() -> Unit)? = null
) {
  val context = LocalContext.current
  val imageBitmap = remember(groupId, members) {
    getGroupAvatar(context, groupId, members)
  }

  val baseModifier = modifier
    .clip(CircleShape)
    .let { if (onClick != null) it.clickable(onClick = onClick) else it }

  Image(
    bitmap = imageBitmap,
    contentDescription = null,
    modifier = baseModifier,
    contentScale = ContentScale.Crop
  )
}

private fun getGroupAvatar(context: Context, groupId: String, members: List<MemberInfo>): androidx.compose.ui.graphics.ImageBitmap {
  val hashStr = members.joinToString(",") { "${it.peerId}:${it.photoBase64?.hashCode() ?: 0}" }
  val key = "${groupId}_$hashStr"

  groupAvatarCache.get(key)?.let { return it }

  val size = 256
  val bitmap = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
  val canvas = Canvas(bitmap)

  val bgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
    color = android.graphics.Color.parseColor("#E1E2E8")
  }
  canvas.drawCircle(size / 2f, size / 2f, size / 2f, bgPaint)

  val count = members.size
  if (count == 0) {
    val bmp = bitmap.asImageBitmap()
    groupAvatarCache.put(key, bmp)
    return bmp
  }

  val drawCount = minOf(count, MAX_GROUP_AVATARS)
  val extraCount = count - MAX_GROUP_AVATARS

  val positions = mutableListOf<Pair<Float, Float>>()
  var radius = 0f

  when (drawCount) {
    1 -> {
      positions.add(size / 2f to size / 2f)
      radius = size / 2f
    }
    2 -> {
      radius = size * 0.35f
      positions.add(size / 2f - radius * 0.6f to size / 2f)
      positions.add(size / 2f + radius * 0.6f to size / 2f)
    }
    3 -> {
      radius = size * 0.32f
      positions.add(size / 2f to size / 2f - radius * 0.6f)
      positions.add(size / 2f - radius * 0.8f to size / 2f + radius * 0.6f)
      positions.add(size / 2f + radius * 0.8f to size / 2f + radius * 0.6f)
    }
    else -> {
      radius = size * 0.28f
      positions.add(size / 2f - radius * 0.8f to size / 2f - radius * 0.8f) // TL
      positions.add(size / 2f + radius * 0.8f to size / 2f - radius * 0.8f) // TR
      positions.add(size / 2f - radius * 0.8f to size / 2f + radius * 0.8f) // BL
      positions.add(size / 2f + radius * 0.8f to size / 2f + radius * 0.8f) // BR
    }
  }

  val strokePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
    color = android.graphics.Color.WHITE
    style = Paint.Style.STROKE
    strokeWidth = size * 0.04f
  }

  val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
    color = android.graphics.Color.WHITE
    textAlign = Paint.Align.CENTER
    typeface = Typeface.DEFAULT_BOLD
  }

  val primaryColors = listOf(
    android.graphics.Color.parseColor("#E53935"),
    android.graphics.Color.parseColor("#1E88E5"),
    android.graphics.Color.parseColor("#43A047"),
    android.graphics.Color.parseColor("#F4511E"),
    android.graphics.Color.parseColor("#8E24AA")
  )

  for (i in 0 until drawCount) {
    val (cx, cy) = positions[i]
    val isLastAndExtra = (i == MAX_GROUP_AVATARS - 1 && extraCount > 0)

    val path = android.graphics.Path().apply {
      addCircle(cx, cy, radius, android.graphics.Path.Direction.CW)
    }

    canvas.save()
    canvas.clipPath(path)

    if (isLastAndExtra) {
      val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = android.graphics.Color.parseColor("#757575")
      }
      canvas.drawRect(cx - radius, cy - radius, cx + radius, cy + radius, paint)
      textPaint.textSize = radius * 0.7f
      canvas.drawText("...", cx, cy + (textPaint.textSize / 3f) - radius * 0.15f, textPaint)
    } else {
      val member = members[i]
      var drawnBmp = false
      if (!member.photoBase64.isNullOrEmpty()) {
        try {
          val bytes = Base64.decode(member.photoBase64, Base64.DEFAULT)
          val bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
          if (bmp != null) {
            val scale = maxOf((radius * 2) / bmp.width, (radius * 2) / bmp.height)
            val scaledW = bmp.width * scale
            val scaledH = bmp.height * scale
            val left = cx - scaledW / 2f
            val top = cy - scaledH / 2f
            val drawRect = RectF(left, top, left + scaledW, top + scaledH)
            canvas.drawBitmap(bmp, null, drawRect, null)
            drawnBmp = true
          }
        } catch (e: Exception) {
          // fallback
        }
      }

      if (!drawnBmp) {
        val colorIdx = kotlin.math.abs(member.name.hashCode()) % primaryColors.size
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = primaryColors[colorIdx] }
        canvas.drawRect(cx - radius, cy - radius, cx + radius, cy + radius, paint)

        val iconPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = android.graphics.Color.WHITE }
        canvas.drawCircle(cx, cy - radius * 0.15f, radius * 0.35f, iconPaint)
        val bodyPath = android.graphics.Path().apply {
          addArc(RectF(cx - radius * 0.65f, cy + radius * 0.25f, cx + radius * 0.65f, cy + radius * 1.55f), 180f, 180f)
        }
        canvas.drawPath(bodyPath, iconPaint)
      }
    }
    canvas.restore()

    if (drawCount > 1) {
      canvas.drawCircle(cx, cy, radius, strokePaint)
    }
  }

  val imageBmp = bitmap.asImageBitmap()
  groupAvatarCache.put(key, imageBmp)
  return imageBmp
}
