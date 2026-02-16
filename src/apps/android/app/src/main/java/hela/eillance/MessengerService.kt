package hela.eillance

// ... imports ...
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.util.Log
import androidx.annotation.Keep
import hela.eillance.camera.R
import hela.eillance.ndkio.CallDirection
import hela.eillance.ndkio.Contact
import hela.eillance.ndkio.ServiceConnectionState
import hela.eillance.service.BackgroundObservers
import hela.eillance.service.CallHandler
import hela.eillance.service.MercuryNotifications
import hela.eillance.service.TelecomIntegration
import hela.eillance.telecom.MercuryConnection
import hela.eillance.viewmodels.CallViewModel
import hela.eillance.webrtc.WebRtcDataTransport
import hela.eillance.webrtc.WebRtcMediaTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout
import org.json.JSONObject
import org.webrtc.VideoSink

class MessengerService : Service() {
  // ... (Class body same as previous patch, except constant below) ...

  class CallServiceBinder : android.os.Binder()

  private val binder = CallServiceBinder()
  private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
  private val engineMutex = Mutex()

  private var wakeLock: PowerManager.WakeLock? = null
  private var dataTransport: WebRtcDataTransport? = null

  // Refactored Components
  private lateinit var notifications: MercuryNotifications
  private lateinit var telecomIntegration: TelecomIntegration
  private lateinit var callHandler: CallHandler
  private lateinit var observers: BackgroundObservers

  private val _errorEvents = MutableSharedFlow<String>()
  val errorEvents: SharedFlow<String> = _errorEvents.asSharedFlow()

  private val _isCoreUnresponsive = MutableStateFlow(false)
  val isCoreUnresponsive = _isCoreUnresponsive.asStateFlow()

  var isIncomingUIVisible: Boolean = false

  override fun onBind(intent: Intent?): IBinder = binder

  override fun onCreate() {
    super.onCreate()
    instance = this

    // Initialize Components
    notifications = MercuryNotifications(this)
    telecomIntegration = TelecomIntegration(this)
    callHandler = CallHandler(this, notifications, serviceScope)
    observers = BackgroundObservers(
      this,
      serviceScope,
      notifications,
      callHandler,
      ::updateForegroundServiceNotification
    )

    callHandler.init()
    telecomIntegration.registerPhoneAccount()

    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        try {
          initEngine(this@MessengerService)
          if (identityExists()) {
            start()
          }
          with(Dispatchers.Main) {
            observers.setup(application)
          }
          _isEngineReady.value = true
          startWatchdog()
        } catch (e: Exception) {
          Log.e(TAG, "Failed to initialize engine", e)
        }
      }
    }

    startForegroundService()
  }

  private fun startWatchdog() {
    serviceScope.launch(Dispatchers.IO) {
      while (true) {
        delay(10000)
        if (_isEngineReady.value) {
          val isAlive = try {
            withTimeout(2000) { nativePing() }
          } catch (e: Exception) {
            false
          }
          _isCoreUnresponsive.value = !isAlive
        }
      }
    }
  }

  fun resolveContactInfo(peerId: String): Contact? {
    return observers.getContact(peerId)
  }

  fun restartCore() {
    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        callHandler.stopLocalMedia()
        nativeRestart()
        _isCoreUnresponsive.value = false
        _errorEvents.emit(getString(R.string.core_reset_success))
      }
    }
  }

  @Keep
  fun onNativeCoreCrash() {
    restartCore()
  }

  // --- Delegation Methods ---

  fun acceptCall() = callHandler.acceptCall()
  fun rejectCall() = callHandler.rejectCall()
  fun hangup() = callHandler.hangup()

  fun onTelecomDisconnect(connection: MercuryConnection) =
    callHandler.onTelecomDisconnect(connection)

  fun startLocalMedia(isVideo: Boolean) = callHandler.startLocalMedia(isVideo)
  fun stopLocalMedia() = callHandler.stopLocalMedia()
  fun setLocalVideoSink(sink: VideoSink?) = callHandler.setLocalVideoSink(sink)
  fun setRemoteVideoSink(peerId: String, sink: VideoSink?) =
    callHandler.setRemoteVideoSink(peerId, sink)

  fun setMicrophoneMute(muted: Boolean) = callHandler.setMicrophoneMute(muted)
  fun setSpeakerphoneOn(on: Boolean) = callHandler.setSpeakerphoneOn(on)

  fun startVoiceCallViaTelecom(targetId: String) =
    telecomIntegration.placeTelecomCall(targetId, false)

  fun startVideoCallViaTelecom(targetId: String) =
    telecomIntegration.placeTelecomCall(targetId, true)

  fun showIncomingCallNotification(peerId: String) =
    notifications.showIncomingCallNotification(peerId)

  // Accessors for Legacy / External Use
  val backgroundCallVM: CallViewModel? get() = observers.callVM

  // --- Lifecycle & Intent Handling ---

  override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
    when (intent?.action) {
      ACTION_REJECT_CALL -> rejectCall()
      ACTION_ACCEPT_CALL -> acceptCall()
      ACTION_HANGUP -> hangup()
      ACTION_MARK_READ -> {
        val peerId = intent.getStringExtra(EXTRA_PEER_ID)
        if (peerId != null) {
          serviceScope.launch {
            MessengerService.isEngineReady.first { it }
            observers.markAllAsRead(peerId)
            notifications.cancelMessageNotification(peerId)
          }
        }
      }
    }
    return START_STICKY
  }

  override fun onDestroy() {
    instance = null
    _isEngineReady.value = false
    callHandler.stop()
    serviceScope.launch(Dispatchers.IO) {
      engineMutex.withLock {
        terminate()
      }
    }
    releaseWakeLock()
    super.onDestroy()
  }

  // --- Transport Setters ---

  @Suppress("unused")
  fun setWebRtcDataTransport(transport: WebRtcDataTransport) {
    dataTransport = transport
    dataTransport?.onError = { errorMessage ->
      serviceScope.launch { _errorEvents.emit(errorMessage) }
    }
  }

  @Suppress("unused")
  fun setWebRtcMediaTransport(transport: WebRtcMediaTransport) {
    callHandler.mediaTransport = transport
    transport.onSendSignalingMessage = { msg ->
      try {
        val json = JSONObject(msg)
        val peerId = json.optString("to")
        if (!peerId.isNullOrEmpty()) {
          dataTransport?.sendDataChannelMessage(peerId, msg)
        }
      } catch (e: Exception) {
        Log.e(TAG, "Failed to route media signaling message", e)
      }
    }
  }

  // --- Internal Logic ---

  fun onActivityStopped() {
    val currentState = callHandler.getLastCallState() ?: observers.callVM?.callState?.value
    if (currentState != null && currentState.isRinging && currentState.callDirection == CallDirection.Incoming) {
      notifications.showIncomingCallNotification(currentState.peerId)
    }
  }

  fun onConnectionCreated(connection: MercuryConnection, peerId: String) {
    callHandler.onConnectionCreated(connection)
  }

  private fun startForegroundService() {
    val notification =
      notifications.buildForegroundServiceNotification(false, null, ServiceConnectionState.Unknown)
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        startForeground(
          101, // ID matching NotificationManager constant
          notification,
          ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        )
      } else {
        startForeground(101, notification)
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start foreground service", e)
    }
  }

  private fun updateForegroundServiceNotification(
    inCall: Boolean,
    peerId: String?,
    connectionState: ServiceConnectionState,
    isVideo: Boolean
  ) {
    notifications.updateForegroundServiceNotification(
      inCall, peerId, connectionState, isVideo
    ) { shouldAcquire ->
      if (shouldAcquire) acquireWakeLock() else releaseWakeLock()
    }
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

  private external fun initEngine(context: Context)
  external fun start()
  private external fun terminate()
  external fun identityExists(): Boolean
  private external fun nativePing(): Boolean
  private external fun nativeRestart()

  companion object {
    var instance: MessengerService? = null
      private set

    private val _isEngineReady = MutableStateFlow(false)
    val isEngineReady = _isEngineReady.asStateFlow()

    private const val TAG = "CallService"
    const val INCOMING_CALL_NOTIFICATION_ID = 102
    const val MESSAGE_NOTIFICATION_ID_BASE = 1000
    const val CALLS_CHANNEL_ID = "incoming_calls_channel_v6"
    const val ACTION_HANGUP = "hela.eillance.ACTION_HANGUP"
    const val ACTION_ACCEPT_CALL = "hela.eillance.ACTION_ACCEPT_CALL"
    const val ACTION_REJECT_CALL = "hela.eillance.ACTION_REJECT_CALL"
    const val ACTION_SHOW_INCOMING_CALL = "hela.eillance.ACTION_SHOW_INCOMING_CALL"
    const val ACTION_MARK_READ = "hela.eillance.ACTION_MARK_READ"
    const val EXTRA_PEER_ID = "hela.eillance.EXTRA_PEER_ID"
  }
}
