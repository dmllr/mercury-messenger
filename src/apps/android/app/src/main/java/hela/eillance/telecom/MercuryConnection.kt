package hela.eillance.telecom

import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.telecom.CallAudioState
import android.telecom.Connection
import android.telecom.DisconnectCause
import android.telecom.PhoneAccount
import android.telecom.TelecomManager
import android.util.Log
import androidx.core.app.NotificationCompat
import hela.eillance.CallService
import hela.eillance.MainActivity
import hela.eillance.camera.R

class MercuryConnection(
  private val callService: CallService,
  private val peerId: String,
  private val isIncoming: Boolean
) : Connection() {

  init {
    connectionProperties = PROPERTY_SELF_MANAGED
    audioModeIsVoip = true
    connectionCapabilities = CAPABILITY_MUTE or CAPABILITY_SUPPORT_HOLD

    setCallerDisplayName(peerId, TelecomManager.PRESENTATION_ALLOWED)
    setAddress(Uri.fromParts(PhoneAccount.SCHEME_SIP, peerId, null), TelecomManager.PRESENTATION_ALLOWED)

    if (!isIncoming) {
      setInitializing()
    }
  }

  override fun onShowIncomingCallUi() {
    Log.i(TAG, "onShowIncomingCallUi for $peerId")

    val fullScreenIntent = Intent(callService, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
      action = CallService.ACTION_SHOW_INCOMING_CALL
      putExtra(CallService.EXTRA_PEER_ID, peerId)
    }

    val pendingIntent = PendingIntent.getActivity(
      callService,
      peerId.hashCode(),
      fullScreenIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val rejectIntent =
      Intent(callService, CallService::class.java).apply { action = "hela.eillance.ACTION_REJECT_CALL" }
    val rejectPendingIntent = PendingIntent.getService(
      callService,
      1,
      rejectIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val acceptIntent = Intent(callService, MainActivity::class.java).apply {
      action = CallService.ACTION_ACCEPT_CALL
      putExtra(CallService.EXTRA_PEER_ID, peerId)
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
    }
    val acceptPendingIntent = PendingIntent.getActivity(
      callService,
      2,
      acceptIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val builder = NotificationCompat.Builder(callService, "incoming_calls_channel")
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setContentTitle(callService.getString(R.string.notification_incoming_call_title))
      .setContentText(peerId)
      .setPriority(NotificationCompat.PRIORITY_MAX)
      .setCategory(NotificationCompat.CATEGORY_CALL)
      .setContentIntent(pendingIntent)
      .setFullScreenIntent(pendingIntent, true)
      .addAction(0, callService.getString(R.string.action_reject), rejectPendingIntent)
      .addAction(0, callService.getString(R.string.action_accept), acceptPendingIntent)
      .setAutoCancel(true)
      .setOngoing(true)

    val notificationManager = callService.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    notificationManager.notify(CallService.INCOMING_CALL_NOTIFICATION_ID, builder.build())

    // Forcibly launch the activity to ensure the incoming call screen is shown immediately
    // rather than waiting for user interaction, effectively waking the screen.
    // As a self-managed ConnectionService in a ringing state, we are permitted to start activities.
    try {
      callService.startActivity(fullScreenIntent)
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start activity for incoming call", e)
    }
  }

  @Deprecated("Deprecated in Java")
  override fun onCallAudioStateChanged(state: CallAudioState?) {
    // Audio focus managed by ConnectionService, routing managed by WebRTC
  }

  override fun onAnswer() {
    cancelNotification()
    callService.acceptCall()
    setActive()
  }

  override fun onReject() {
    cancelNotification()
    callService.rejectCall()
    setDisconnected(DisconnectCause(DisconnectCause.REJECTED))
    callService.unregisterTelecomConnection(peerId)
    destroy()
  }

  override fun onDisconnect() {
    cancelNotification()
    callService.hangup()
    setDisconnected(DisconnectCause(DisconnectCause.LOCAL))
    callService.unregisterTelecomConnection(peerId)
    destroy()
  }

  override fun onAbort() {
    cancelNotification()
    super.onAbort()
    callService.unregisterTelecomConnection(peerId)
    destroy()
  }

  fun onRemoteHangup() {
    cancelNotification()
    setDisconnected(DisconnectCause(DisconnectCause.REMOTE))
    destroy()
  }

  fun onEstablished() {
    cancelNotification()
    setActive()
  }

  private fun cancelNotification() {
    val notificationManager = callService.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
    notificationManager.cancel(CallService.INCOMING_CALL_NOTIFICATION_ID)
  }

  companion object {
    private const val TAG = "MercuryConnection"
  }
}
