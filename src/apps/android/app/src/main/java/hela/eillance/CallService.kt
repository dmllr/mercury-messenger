package hela.eillance

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.ComponentName
import android.content.Context
import android.content.Intent
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
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
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

  private var wakeLock: PowerManager.WakeLock? = null
  private var webRtcTransport: WebRtcTransport? = null

  // Flag to signal engine readiness
  private val _isEngineReady = MutableStateFlow(false)
  val isEngineReady = _isEngineReady.asStateFlow()

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

    // Initialize Engine on Main thread synchronously to avoid race conditions with UI
    // The engine constructor mainly does JNI env setup and pointer allocation.
    // Heavy DB operations are internal to the C++ objects but `new Client` is generally fast enough.
    // Ideally this should be async but we need to block UI or have UI wait.
    try {
      initEngine(this@CallService)
      _isEngineReady.value = true
      if (identityExists()) {
        start()
      }
      setupBackgroundObservers()
    } catch (e: Exception) {
      Log.e(TAG, "Failed to initialize engine", e)
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
    phoneAccountHandle = PhoneAccountHandle(componentName, "Mercury")
    val builder = PhoneAccount.builder(phoneAccountHandle, "Mercury")
      .setCapabilities(PhoneAccount.CAPABILITY_SELF_MANAGED)
      .setShortDescription("Mercury Secure Call")
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
      try {
        telecomManager.registerPhoneAccount(builder.build())
      } catch (e: SecurityException) {
        Log.e(TAG, "Failed to register PhoneAccount", e)
      }
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
    soundPlayer.stop()
    serviceScope.cancel()
    terminate()
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
        showNewMessageNotification(newContact.peerId, "New message from ${newContact.name}")
      }
    }
  }

  private fun startForegroundService() {
    val notification = buildForegroundServiceNotification(false, null, ServiceConnectionState.Unknown)
    try {
      startForeground(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start foreground service", e)
    }
  }

  private fun updateForegroundServiceNotification(inCall: Boolean, peerId: String?, connectionState: ServiceConnectionState) {
    if (inCall) acquireWakeLock() else releaseWakeLock()
    val notification = buildForegroundServiceNotification(inCall, peerId, connectionState)
    notificationManager.notify(FOREGROUND_SERVICE_NOTIFICATION_ID, notification)
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
      .setContentTitle("Incoming Call")
      .setContentText(peerId)
      .setPriority(NotificationCompat.PRIORITY_MAX)
      .setCategory(NotificationCompat.CATEGORY_CALL)
      .setFullScreenIntent(fullScreenPendingIntent, true)
      .addAction(0, "Reject", rejectPendingIntent)
      .addAction(0, "Accept", acceptPendingIntent)
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
      builder.setContentText("In call with ${peerId ?: "Unknown"}")
        .addAction(0, "Hang Up", hangupPendingIntent)
    } else {
      val contentText = when (connectionState) {
        ServiceConnectionState.Online -> "Ready"
        ServiceConnectionState.Establishing -> "Connecting..."
        ServiceConnectionState.Failure -> "Connection failed"
        else -> "Background Service"
      }
      builder.setContentText(contentText)
    }
    return builder.build()
  }

  private fun createNotificationChannels() {
    val foregroundChannel = NotificationChannel(FOREGROUND_CHANNEL_ID, "Foreground Service", NotificationManager.IMPORTANCE_MIN)
    val callsChannel = NotificationChannel(CALLS_CHANNEL_ID, "Incoming Calls", NotificationManager.IMPORTANCE_HIGH).apply {
      description = "Notifications for incoming calls"
    }
    val messagesChannel = NotificationChannel(MESSAGES_CHANNEL_ID, "New Messages", NotificationManager.IMPORTANCE_DEFAULT)
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
