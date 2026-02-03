package hela.eillance

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.os.PowerManager
import android.telecom.PhoneAccount
import android.telecom.PhoneAccountHandle
import android.telecom.TelecomManager
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.Person
import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.CallState
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.ndkio.VideoConfig
import hela.eillance.telecom.MercuryConnection
import hela.eillance.telecom.MercuryConnectionService
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.viewmodels.ContactsViewModel
import hela.eillance.viewmodels.SettingsViewModel
import hela.eillance.webrtc.WebRtcTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.webrtc.VideoSink
import java.util.concurrent.ConcurrentHashMap

class CallService : Service() {

  inner class CallServiceBinder : android.os.Binder() {
    fun getService(): CallService = this@CallService
  }

  private val binder = CallServiceBinder()
  private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
  private val notificationManager by lazy { getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager }
  private val telecomManager by lazy { getSystemService(Context.TELECOM_SERVICE) as TelecomManager }
  private val engineMutex = Mutex()

  private var wakeLock: PowerManager.WakeLock? = null
  private var webRtcTransport: WebRtcTransport? = null

  // Background ViewModels
  private var backgroundContactsVM: ContactsViewModel? = null
  private var backgroundCallVM: CallViewModel? = null
  private var backgroundSettingsVM: SettingsViewModel? = null

  private var lastKnownCallState: CallState? = null
  private var lastKnownContacts: List<Contact>? = null

  private val soundPlayer: CallSoundPlayer by lazy { CallSoundPlayer(this) }
  private lateinit var phoneAccountHandle: PhoneAccountHandle
  private val activeTelecomConnections = ConcurrentHashMap<String, MercuryConnection>()

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    instance = this
    createNotificationChannels()
    registerPhoneAccount()

    // Initialize Engine on Background thread to avoid ANR.
    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        try {
          initEngine(this@CallService)
          if (identityExists()) {
            start()
          }
          setupBackgroundObservers()
          _isEngineReady.value = true
        } catch (e: Exception) {
          Log.e(TAG, "Failed to initialize engine", e)
        }
      }
    }

    startForegroundService()
  }

  private fun setupBackgroundObservers() {
    val app = application
    backgroundContactsVM = ContactsViewModel(app)
    backgroundCallVM = CallViewModel(app)
    backgroundSettingsVM = SettingsViewModel(app)

    serviceScope.launch {
      launch {
        backgroundContactsVM?.contacts?.collect { contacts ->
          checkForNewMessages(lastKnownContacts, contacts)
          lastKnownContacts = contacts
        }
      }
      launch {
        backgroundCallVM?.callState?.collect { state ->
          handleCallStateChange(state)
        }
      }
      launch {
        backgroundSettingsVM?.connectionState?.collect { state ->
          updateForegroundServiceNotification(lastKnownCallState?.isInCall == true, lastKnownCallState?.peerId, state)
        }
      }
    }
  }

  fun acceptCall() {
    notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    backgroundCallVM?.acceptCall()
  }

  fun rejectCall() {
    notificationManager.cancel(INCOMING_CALL_NOTIFICATION_ID)
    backgroundCallVM?.rejectCall()
  }

  fun hangup() {
    backgroundCallVM?.hangup()
  }

  fun startLocalMedia() = webRtcTransport?.startLocalMedia()
  fun stopLocalMedia() = webRtcTransport?.stopLocalMedia()
  fun setLocalVideoSink(sink: VideoSink?) = webRtcTransport?.setLocalSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) = webRtcTransport?.setRemoteSink(peerId, sink)

  private fun registerPhoneAccount() {
    val componentName = ComponentName(this, MercuryConnectionService::class.java)
    phoneAccountHandle = PhoneAccountHandle(componentName, getString(R.string.telecom_account_name))
    val builder = PhoneAccount.builder(phoneAccountHandle, getString(R.string.telecom_account_name))
      .setCapabilities(PhoneAccount.CAPABILITY_SELF_MANAGED)
      .setShortDescription(getString(R.string.telecom_account_description))
    try {
      telecomManager.registerPhoneAccount(builder.build())
    } catch (e: SecurityException) {
      Log.e(TAG, "Failed to register PhoneAccount", e)
    }
  }

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_REJECT_CALL -> rejectCall()
      ACTION_HANGUP -> hangup()
    }
    return START_STICKY
  }

  override fun onDestroy() {
    instance = null
    _isEngineReady.value = false
    soundPlayer.stop()

    // We must launch a blocking run or a separate scope to ensure terminate happens,
    // but since we are in onDestroy, the process might be dying.
    // Ideally, we block briefly to ensure clean C++ shutdown.
    // However, on Android, it's often safer to let the OS kill the process if we are going down,
    // to avoid the exact race condition you saw.
    // We try to clean up if possible.
    val shutdownJob = serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        terminate()
      }
    }

    // We can't easily wait for this job in onDestroy without blocking UI thread potentially.
    // But since serviceScope is Supervisor + Main, we just let it trigger.

    releaseWakeLock()
    super.onDestroy()
  }

  @Suppress("unused")
  fun setWebRtcTransport(transport: WebRtcTransport) {
    webRtcTransport = transport
  }

  private fun handleCallStateChange(callState: CallState) {
    syncTelecomState(callState)
    soundPlayer.updateCallState(callState)
    checkForIncomingCall(lastKnownCallState, callState)
    updateForegroundServiceNotification(callState.isInCall, callState.peerId, ServiceConnectionState.Online)
    lastKnownCallState = callState
  }

  private fun syncTelecomState(callState: CallState) {
    val currentActivePeerId = if (callState.isConnecting || callState.isRinging || callState.isInCall) {
      callState.peerId.takeIf { it.isNotEmpty() }
    } else {
      null
    }

    val iterator = activeTelecomConnections.iterator()
    while (iterator.hasNext()) {
      val entry = iterator.next()
      val trackedPeerId = entry.key
      val connection = entry.value

      if (trackedPeerId == currentActivePeerId) {
        if (callState.isInCall) {
          if (connection.state != android.telecom.Connection.STATE_ACTIVE) {
            connection.onEstablished()
          }
        }
      } else {
        connection.onRemoteHangup()
        iterator.remove()
      }
    }
  }

  fun registerTelecomConnection(peerId: String, connection: MercuryConnection) {
    val existing = activeTelecomConnections.put(peerId, connection)
    existing?.onRemoteHangup()
  }

  fun unregisterTelecomConnection(peerId: String) {
    activeTelecomConnections.remove(peerId)
  }

  private fun checkForIncomingCall(oldState: CallState?, newState: CallState) {
    val wasRinging = oldState?.isRinging ?: false
    if (!wasRinging && newState.isRinging && newState.callDirection == CallDirection.Incoming) {
      try {
        val extras = Bundle()
        extras.putString(MercuryConnectionService.EXTRA_PEER_ID, newState.peerId)
        val uri = Uri.fromParts(PhoneAccount.SCHEME_SIP, newState.peerId, null)
        extras.putParcelable(TelecomManager.EXTRA_INCOMING_CALL_ADDRESS, uri)
        val nestedExtras = Bundle()
        nestedExtras.putString(MercuryConnectionService.EXTRA_PEER_ID, newState.peerId)
        extras.putBundle(TelecomManager.EXTRA_INCOMING_CALL_EXTRAS, nestedExtras)
        telecomManager.addNewIncomingCall(phoneAccountHandle, extras)
      } catch (e: Exception) {
        Log.e(TAG, "Failed to add incoming call to Telecom", e)
        showIncomingCallNotification(newState.peerId)
      }
    }
  }

  private fun checkForNewMessages(oldContacts: List<Contact>?, newContacts: List<Contact>) {
    if (oldContacts == null) return
    val oldUnreadCounts = oldContacts.associate { it.peerId to it.unreadCount }
    newContacts.forEach { newContact ->
      val oldUnread = oldUnreadCounts[newContact.peerId] ?: 0
      if (newContact.unreadCount > oldUnread && !MercuryApplication.isAppInForeground) {
        showNewMessageNotification(newContact.peerId, getString(R.string.notification_new_message_from, newContact.name))
      }
    }
  }

  private fun startForegroundService() {
    // Initial start: We are NOT in a call, so we use CONNECTED_DEVICE.
    // This avoids the SecurityException because we don't need RECORD_AUDIO yet.
    val notification = buildForegroundServiceNotification(false, null, ServiceConnectionState.Unknown)
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
      } else {
        startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start foreground service", e)
    }
  }

  private fun updateForegroundServiceNotification(inCall: Boolean, peerId: String?, connectionState: ServiceConnectionState) {
    if (inCall) acquireWakeLock() else releaseWakeLock()

    val notification = buildForegroundServiceNotification(inCall, peerId, connectionState)

    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        // If we are in a call, we upgrade to MICROPHONE + CAMERA (if video) + CONNECTED_DEVICE
        val serviceType = if (inCall) {
          // TODO: check runtime permissions here before applying these types
          var type = ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE or ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
          if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
          }
          type
        } else {
          ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        }
        startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification, serviceType)
      } else {
        notificationManager.notify(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to update foreground service notification", e)
    }
  }

  private fun showIncomingCallNotification(peerId: String) {
    val fullScreenIntent = Intent(this, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
      action = ACTION_SHOW_INCOMING_CALL
      putExtra(EXTRA_PEER_ID, peerId)
    }
    val fullScreenPendingIntent = PendingIntent.getActivity(this, 0, fullScreenIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val rejectIntent = Intent(this, CallService::class.java).apply { action = ACTION_REJECT_CALL }
    val rejectPendingIntent = PendingIntent.getService(this, 1, rejectIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val acceptIntent = Intent(this, MainActivity::class.java).apply {
      action = ACTION_ACCEPT_CALL
      putExtra(EXTRA_PEER_ID, peerId)
      flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
    }
    val acceptPendingIntent = PendingIntent.getActivity(this, 2, acceptIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)

    val builder = NotificationCompat.Builder(this, CALLS_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setContentTitle(getString(R.string.notification_incoming_call_title))
      .setContentText(peerId)
      .setPriority(NotificationCompat.PRIORITY_MAX)
      .setCategory(NotificationCompat.CATEGORY_CALL)
      .setFullScreenIntent(fullScreenPendingIntent, true)
      .addAction(0, getString(R.string.action_reject), rejectPendingIntent)
      .addAction(0, getString(R.string.action_accept), acceptPendingIntent)
      .setAutoCancel(true)
      .setOngoing(true)

    notificationManager.notify(INCOMING_CALL_NOTIFICATION_ID, builder.build())
    if (MercuryApplication.isAppInForeground) {
      startActivity(fullScreenIntent)
    }
  }

  private fun showNewMessageNotification(peerId: String, content: String) {
    val intent = Intent(this, MainActivity::class.java).apply {
      flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
      putExtra(EXTRA_PEER_ID, peerId)
    }
    val pendingIntent = PendingIntent.getActivity(
      this,
      MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(),
      intent,
      PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    val person = Person.Builder().setName(peerId).build()
    val messagingStyle = NotificationCompat.MessagingStyle(person)
      .addMessage(content, System.currentTimeMillis(), person)
    val builder = NotificationCompat.Builder(this, MESSAGES_CHANNEL_ID)
      .setSmallIcon(android.R.drawable.stat_notify_chat)
      .setStyle(messagingStyle)
      .setContentIntent(pendingIntent)
      .setAutoCancel(true)
    notificationManager.notify(MESSAGE_NOTIFICATION_ID_BASE + peerId.hashCode(), builder.build())
  }

  private fun buildForegroundServiceNotification(inCall: Boolean, peerId: String?, connectionState: ServiceConnectionState): Notification {
    val notificationIntent = Intent(this, MainActivity::class.java)
    val pendingIntent = PendingIntent.getActivity(this, 0, notificationIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
    val builder = NotificationCompat.Builder(this, FOREGROUND_CHANNEL_ID)
      .setContentTitle(getString(R.string.app_name))
      .setSmallIcon(R.drawable.ic_status_standby)
      .setContentIntent(pendingIntent)

    if (inCall) {
      val hangupIntent = Intent(this, CallService::class.java).apply { action = ACTION_HANGUP }
      val hangupPendingIntent = PendingIntent.getService(this, 3, hangupIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
      builder.setContentText(getString(R.string.service_status_in_call_with, peerId ?: getString(R.string.unknown)))
        .addAction(0, getString(R.string.action_hangup), hangupPendingIntent)
    } else {
      val contentText = when (connectionState) {
        ServiceConnectionState.Online -> getString(R.string.service_status_ready)
        ServiceConnectionState.Establishing -> getString(R.string.service_status_connecting)
        ServiceConnectionState.NoNetwork -> getString(R.string.service_status_no_network)
        ServiceConnectionState.Failure -> getString(R.string.service_status_connection_failed)
        else -> getString(R.string.service_status_background)
      }
      builder.setContentText(contentText)
    }
    return builder.build()
  }

  private fun createNotificationChannels() {
    val foregroundChannel = NotificationChannel(FOREGROUND_CHANNEL_ID, getString(R.string.notification_channel_foreground_title), NotificationManager.IMPORTANCE_MIN)
    val callsChannel = NotificationChannel(CALLS_CHANNEL_ID, getString(R.string.notification_channel_calls_title), NotificationManager.IMPORTANCE_HIGH).apply {
      description = getString(R.string.notification_channel_calls_description)
    }
    val messagesChannel = NotificationChannel(MESSAGES_CHANNEL_ID, getString(R.string.notification_channel_messages_title), NotificationManager.IMPORTANCE_DEFAULT)
    notificationManager.createNotificationChannel(foregroundChannel)
    notificationManager.createNotificationChannel(callsChannel)
    notificationManager.createNotificationChannel(messagesChannel)
  }

  private fun acquireWakeLock() {
    if (wakeLock == null) {
      val powerManager = getSystemService(Context.POWER_SERVICE) as PowerManager
      wakeLock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "Mercury::CallWakeLock")
      wakeLock?.setReferenceCounted(false)
      wakeLock?.acquire(10 * 60 * 1000L)
    }
  }

  private fun releaseWakeLock() {
    wakeLock?.let { if (it.isHeld) it.release() }
    wakeLock = null
  }

  fun startVoiceCallViaTelecom(targetId: String) {
    placeTelecomCall(targetId, false)
  }

  fun startVideoCallViaTelecom(targetId: String) {
    placeTelecomCall(targetId, true)
  }

  private fun placeTelecomCall(targetId: String, isVideo: Boolean) {
    if (activeTelecomConnections.containsKey(targetId)) return
    try {
      val extras = Bundle()
      extras.putString(MercuryConnectionService.EXTRA_PEER_ID, targetId)
      val uri = Uri.fromParts(PhoneAccount.SCHEME_SIP, targetId, null)
      val extrasBundle = Bundle()
      extrasBundle.putParcelable(TelecomManager.EXTRA_PHONE_ACCOUNT_HANDLE, phoneAccountHandle)
      extrasBundle.putBundle(TelecomManager.EXTRA_OUTGOING_CALL_EXTRAS, extras)
      if (isVideo) {
        extrasBundle.putInt(TelecomManager.EXTRA_START_CALL_WITH_VIDEO_STATE, android.telecom.VideoProfile.STATE_BIDIRECTIONAL)
      }
      telecomManager.placeCall(uri, extrasBundle)
    } catch (e: SecurityException) {
      Log.e(TAG, "Permission missing for placeCall", e)
    }
  }

  private external fun initEngine(context: Context)
  external fun start()
  private external fun terminate()
  external fun identityExists(): Boolean
  external fun configureVideoCapture(config: VideoConfig)

  companion object {
    var instance: CallService? = null
      private set

    // Flag to signal engine readiness, exposed statically so UI can observe it
    // even before Service instance is fully attached/observed via other means.
    private val _isEngineReady = MutableStateFlow(false)
    val isEngineReady = _isEngineReady.asStateFlow()

    private const val TAG = "CallService"
    private const val FOREGROUND_SERVICE_NOTIFICATION_ID = 101
    const val INCOMING_CALL_NOTIFICATION_ID = 102
    const val MESSAGE_NOTIFICATION_ID_BASE = 1000
    private const val FOREGROUND_CHANNEL_ID = "foreground_service_channel"
    private const val CALLS_CHANNEL_ID = "incoming_calls_channel"
    private const val MESSAGES_CHANNEL_ID = "messages_channel"
    private const val ACTION_HANGUP = "hela.eillance.ACTION_HANGUP"
    const val ACTION_ACCEPT_CALL = "hela.eillance.ACTION_ACCEPT_CALL"
    private const val ACTION_REJECT_CALL = "hela.eillance.ACTION_REJECT_CALL"
    const val ACTION_SHOW_INCOMING_CALL = "hela.eillance.ACTION_SHOW_INCOMING_CALL"
    const val EXTRA_PEER_ID = "hela.eillance.EXTRA_PEER_ID"
  }
}
