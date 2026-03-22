package hela.eillance.service

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.graphics.Color
import android.os.Build
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.Person
import androidx.core.content.ContextCompat
import hela.eillance.IncomingCallActivity
import hela.eillance.MercuryApplication
import hela.eillance.MessengerActivity
import hela.eillance.MessengerService
import hela.eillance.camera.R
import hela.eillance.ndkio.AttachmentType
import hela.eillance.ndkio.HistoryEntry
import hela.eillance.ndkio.ServiceConnectionState

class MercuryNotifications(private val service: MessengerService) {

  private val notificationManager =
    service.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

  init {
    createNotificationChannels()
  }

  fun cancelIncomingCallNotification() {
    notificationManager.cancel(MessengerService.INCOMING_CALL_NOTIFICATION_ID)
  }

  fun showIncomingCallNotification(peerId: String) {
    if (MercuryApplication.isAppInForeground) {
      return
    }

    val fullScreenIntent = Intent(service, IncomingCallActivity::class.java).apply {
      flags =
        Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK or Intent.FLAG_ACTIVITY_NO_USER_ACTION
      action = MessengerService.ACTION_SHOW_INCOMING_CALL
      putExtra(MessengerService.EXTRA_PEER_ID, peerId)
    }

    val fullScreenPendingIntent = PendingIntent.getActivity(
      service,
      12345,
      fullScreenIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val rejectIntent = Intent(service, MessengerService::class.java).apply {
      action = MessengerService.ACTION_REJECT_CALL
    }
    val rejectPendingIntent = PendingIntent.getService(
      service,
      1,
      rejectIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val acceptIntent = Intent(service, MessengerActivity::class.java).apply {
      action = MessengerService.ACTION_ACCEPT_CALL
      putExtra(MessengerService.EXTRA_PEER_ID, peerId)
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP
    }
    val acceptPendingIntent = PendingIntent.getActivity(
      service,
      2,
      acceptIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val contact = service.resolveContactInfo(peerId)
    val displayName = if (!contact?.name.isNullOrBlank()) {
      "${contact.name} (${peerId.take(8)}...)"
    } else {
      "${peerId.take(8)}..."
    }

    @SuppressLint("FullScreenIntentPolicy")
    val builder = NotificationCompat.Builder(service, MessengerService.CALLS_CHANNEL_ID)
      .setSmallIcon(R.drawable.ic_status_attention)
      .setContentTitle(service.getString(R.string.notification_incoming_call_title))
      .setContentText(displayName)
      .setPriority(NotificationCompat.PRIORITY_MAX)
      .setCategory(NotificationCompat.CATEGORY_CALL)
      .setFullScreenIntent(fullScreenPendingIntent, true)
      .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
      .addAction(
        R.drawable.ic_status_attention,
        service.getString(R.string.action_reject),
        rejectPendingIntent
      )
      .addAction(
        R.drawable.ic_status_standby,
        service.getString(R.string.action_accept),
        acceptPendingIntent
      )
      .setAutoCancel(false)
      .setOngoing(true)
      .setVibrate(longArrayOf(0, 1000, 1000))
      .setLights(Color.WHITE, 500, 500)

    if (!MercuryApplication.isAppInForeground) {
      try {
        service.startActivity(fullScreenIntent)
      } catch (e: Exception) {
        Log.e(TAG, "Failed to force start incoming call activity (background restriction)", e)
      }
    }

    notificationManager.notify(MessengerService.INCOMING_CALL_NOTIFICATION_ID, builder.build())
  }

  fun showNewMessageNotification(
    peerId: String,
    title: String,
    messages: List<HistoryEntry>,
    showContent: Boolean
  ) {
    val intent = Intent(service, MessengerActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
      putExtra(MessengerService.EXTRA_PEER_ID, peerId)
    }
    val pendingIntent = PendingIntent.getActivity(
      service,
      MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(),
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    val me = Person.Builder().setName(service.getString(R.string.app_name)).build()
    val sender = Person.Builder().setName(title).build()
    val style = NotificationCompat.MessagingStyle(me)

    if (messages.isNotEmpty()) {
      messages.forEach { msg ->
        if (msg.isDeleted) return@forEach
        val text = if (showContent) {
          if (!msg.content.isNullOrBlank()) msg.content
          else if (msg.attachments.isNotEmpty()) {
            val att = msg.attachments.first()
            val typeText = when (att.type) {
              AttachmentType.Image -> service.getString(R.string.file_type_image)
              AttachmentType.Video -> service.getString(R.string.file_type_video)
              AttachmentType.File -> if (att.mime.startsWith("audio/")) service.getString(R.string.file_type_audio) else service.getString(
                R.string.file_type_generic
              )
            }
            "[$typeText]"
          } else service.getString(R.string.notification_new_message)
        } else service.getString(R.string.notification_new_message)
        style.addMessage(text, msg.timestamp / 1000, sender)
      }
    } else {
      style.addMessage(
        service.getString(R.string.notification_new_message),
        System.currentTimeMillis(),
        sender
      )
    }

    val markReadIntent = Intent(service, MessengerService::class.java).apply {
      action = MessengerService.ACTION_MARK_READ
      putExtra(MessengerService.EXTRA_PEER_ID, peerId)
    }
    val markReadPendingIntent = PendingIntent.getService(
      service,
      peerId.hashCode(),
      markReadIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    val markReadAction =
      NotificationCompat.Action.Builder(
        0,
        service.getString(R.string.action_mark_read),
        markReadPendingIntent
      ).build()

    val builder = NotificationCompat.Builder(service, MESSAGES_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setStyle(style)
      .setShowWhen(false)
      .setContentIntent(pendingIntent)
      .addAction(markReadAction)
      .setAutoCancel(true)
    notificationManager.notify(
      MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(),
      builder.build()
    )
  }

  fun showContactRequestNotification(peerId: String, title: String) {
    val intent = Intent(service, MessengerActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
      putExtra(MessengerService.EXTRA_PEER_ID, peerId)
    }
    val pendingIntent = PendingIntent.getActivity(
      service,
      MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode() + 1,
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val builder = NotificationCompat.Builder(service, MESSAGES_CHANNEL_ID)
      .setSmallIcon(R.drawable.ic_status_attention)
      .setContentTitle(service.getString(R.string.sys_contact_req))
      .setContentText(title)
      .setAutoCancel(true)
      .setContentIntent(pendingIntent)

    notificationManager.notify(MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode() + 10000, builder.build())
  }

  fun showMissedCallNotification(peerId: String, title: String) {
    val intent = Intent(service, MessengerActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
      putExtra(MessengerService.EXTRA_PEER_ID, peerId)
    }
    val pendingIntent = PendingIntent.getActivity(
      service,
      MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode() + 2,
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )

    val builder = NotificationCompat.Builder(service, MESSAGES_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_missed_call)
      .setContentTitle(service.getString(R.string.call_missed))
      .setContentText(title)
      .setAutoCancel(true)
      .setContentIntent(pendingIntent)

    notificationManager.notify(MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode() + 20000, builder.build())
  }

  fun updateForegroundServiceNotification(
    inCall: Boolean,
    peerId: String?,
    connectionState: ServiceConnectionState,
    isVideo: Boolean,
    wakeLockAcquirer: (Boolean) -> Unit
  ) {
    wakeLockAcquirer(inCall)
    val notification = buildForegroundServiceNotification(inCall, peerId, connectionState)
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        val serviceType = if (inCall) {
          var type = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
          if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (ContextCompat.checkSelfPermission(
                service,
                Manifest.permission.RECORD_AUDIO
              ) == PackageManager.PERMISSION_GRANTED
            ) {
              type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            }
            val hasCamera =
              ContextCompat.checkSelfPermission(
                service,
                Manifest.permission.CAMERA
              ) == PackageManager.PERMISSION_GRANTED
            if (isVideo && hasCamera) {
              type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
            }
          }
          type
        } else {
          ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        }
        service.startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification, serviceType)
      } else {
        service.startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to update foreground service notification", e)
    }
  }

  fun buildForegroundServiceNotification(
    inCall: Boolean,
    peerId: String?,
    connectionState: ServiceConnectionState
  ): Notification {
    val isSilent = !inCall && (connectionState == ServiceConnectionState.Online ||
        connectionState == ServiceConnectionState.Establishing ||
        connectionState == ServiceConnectionState.Unknown)

    val channelId = if (isSilent) FOREGROUND_CHANNEL_SILENT_ID else FOREGROUND_CHANNEL_ALERT_ID
    val priority = if (isSilent) NotificationCompat.PRIORITY_MIN else NotificationCompat.PRIORITY_LOW

    val notificationIntent = Intent(service, MessengerActivity::class.java)
    val pendingIntent = PendingIntent.getActivity(
      service,
      0,
      notificationIntent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    val builder = NotificationCompat.Builder(service, channelId)
      .setContentTitle(service.getString(R.string.app_name))
      .setSmallIcon(R.drawable.ic_status_standby)
      .setContentIntent(pendingIntent)
      .setPriority(priority)
      .setSilent(isSilent)

    if (inCall) {
      val hangupIntent = Intent(service, MessengerService::class.java).apply {
        action = MessengerService.ACTION_HANGUP
      }
      val hangupPendingIntent = PendingIntent.getService(
        service,
        3,
        hangupIntent,
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
      )
      builder.setContentText(
        service.getString(
          R.string.service_status_in_call_with,
          peerId ?: service.getString(R.string.unknown)
        )
      )
        .addAction(0, service.getString(R.string.action_hangup), hangupPendingIntent)
    } else {
      val contentText = when (connectionState) {
        ServiceConnectionState.Online -> service.getString(R.string.service_status_ready)
        ServiceConnectionState.Establishing -> service.getString(R.string.service_status_connecting)
        ServiceConnectionState.NoNetwork -> service.getString(R.string.service_status_no_network)
        ServiceConnectionState.Failure -> service.getString(R.string.service_status_connection_failed)
        else -> service.getString(R.string.service_status_background)
      }
      builder.setContentText(contentText)
    }
    return builder.build()
  }

  private fun createNotificationChannels() {
    val silentChannel = NotificationChannel(
      FOREGROUND_CHANNEL_SILENT_ID,
      service.getString(R.string.notification_channel_foreground_title),
      NotificationManager.IMPORTANCE_MIN
    ).apply {
      setShowBadge(false)
      lockscreenVisibility = Notification.VISIBILITY_SECRET
    }

    val alertChannel = NotificationChannel(
      FOREGROUND_CHANNEL_ALERT_ID,
      service.getString(R.string.service_status_background),
      NotificationManager.IMPORTANCE_LOW
    ).apply {
      setShowBadge(true)
      lockscreenVisibility = Notification.VISIBILITY_PUBLIC
    }

    val callsChannel = NotificationChannel(
      MessengerService.CALLS_CHANNEL_ID,
      service.getString(R.string.notification_channel_calls_title),
      NotificationManager.IMPORTANCE_HIGH
    ).apply {
      description = service.getString(R.string.notification_channel_calls_description)
      setSound(null, null)
      enableVibration(true)
      vibrationPattern = longArrayOf(0, 500, 500)
      lockscreenVisibility = Notification.VISIBILITY_PUBLIC
    }

    val messagesChannel = NotificationChannel(
      MESSAGES_CHANNEL_ID,
      service.getString(R.string.notification_channel_messages_title),
      NotificationManager.IMPORTANCE_DEFAULT
    )

    notificationManager.createNotificationChannel(silentChannel)
    notificationManager.createNotificationChannel(alertChannel)
    notificationManager.createNotificationChannel(callsChannel)
    notificationManager.createNotificationChannel(messagesChannel)
  }

  fun cancelMessageNotification(peerId: String) {
    notificationManager.cancel(MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode())
    notificationManager.cancel(MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode() + 10000)
    notificationManager.cancel(MessengerService.MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode() + 20000)
  }

  companion object {
    private const val TAG = "MercuryNotifications"
    private const val FOREGROUND_CHANNEL_ALERT_ID = "foreground_service_channel_status"
    private const val FOREGROUND_CHANNEL_SILENT_ID = "foreground_service_channel"
    private const val MESSAGES_CHANNEL_ID = "messages_channel"
    private const val FOREGROUND_SERVICE_NOTIFICATION_ID = 101
  }
}
