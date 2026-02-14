package hela.eillance.screens.conversation

import android.content.Context
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.CallMade
import androidx.compose.material.icons.automirrored.filled.CallMissed
import androidx.compose.material.icons.automirrored.filled.CallReceived
import androidx.compose.material.icons.filled.Call
import androidx.compose.ui.graphics.vector.ImageVector
import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallStatus
import hela.eillance.ndkio.HistoryEntryKind
import hela.eillance.ui.LocalizationMapping
import java.util.concurrent.TimeUnit

object CallUtils {

  fun getCallIconInfo(
    direction: CallDirection?,
    status: CallStatus?
  ): Triple<ImageVector, Boolean, Int> {
    // Returns <Icon, IsErrorColor, DescriptionResId>
    return when (direction) {
      CallDirection.Outgoing -> when (status) {
        CallStatus.Answered -> Triple(Icons.AutoMirrored.Filled.CallMade, false, R.string.call_outgoing_voice)
        else -> Triple(Icons.AutoMirrored.Filled.CallMade, true, R.string.call_outgoing_voice)
      }

      CallDirection.Incoming -> when (status) {
        CallStatus.Missed -> Triple(Icons.AutoMirrored.Filled.CallMissed, true, R.string.call_missed)
        CallStatus.Answered -> Triple(Icons.AutoMirrored.Filled.CallReceived, false, R.string.call_incoming_voice)
        else -> Triple(Icons.AutoMirrored.Filled.CallReceived, false, R.string.call_rejected)
      }

      null -> Triple(Icons.Default.Call, false, R.string.call_generic)
    }
  }

  fun formatCallText(
    context: Context,
    kind: HistoryEntryKind,
    direction: CallDirection?,
    status: CallStatus?,
    durationSeconds: Int
  ): String {
    val baseDescription = if (status == CallStatus.Missed) {
      context.getString(R.string.call_missed)
    } else if (status == CallStatus.Rejected) {
      context.getString(R.string.call_rejected)
    } else {
      val dirInt = if (direction == CallDirection.Incoming) 1 else 0
      context.getString(LocalizationMapping.getCallDescriptionResId(kind, dirInt))
    }

    val durationText = if (status == CallStatus.Answered && durationSeconds > 0) {
      val duration = durationSeconds.toLong()
      val minutes = TimeUnit.SECONDS.toMinutes(duration)
      val seconds = duration - TimeUnit.MINUTES.toSeconds(minutes)
      " " + context.getString(R.string.call_duration_format_short, minutes, seconds)
    } else ""

    return "$baseDescription$durationText"
  }
}
