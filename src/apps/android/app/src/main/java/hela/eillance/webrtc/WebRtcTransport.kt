package hela.eillance.webrtc

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.util.Log
import androidx.core.content.ContextCompat
import hela.eillance.ndkio.PeerConnectionState
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
import org.json.JSONObject
import org.webrtc.AudioTrack
import org.webrtc.Camera2Enumerator
import org.webrtc.DataChannel
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.DefaultVideoEncoderFactory
import org.webrtc.EglBase
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.MediaStream
import org.webrtc.PeerConnection
import org.webrtc.PeerConnection.RTCConfiguration
import org.webrtc.PeerConnectionFactory
import org.webrtc.RtpReceiver
import org.webrtc.RtpTransceiver
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import org.webrtc.SurfaceTextureHelper
import org.webrtc.VideoCapturer
import org.webrtc.VideoSink
import org.webrtc.VideoSource
import org.webrtc.VideoTrack
import org.webrtc.audio.AudioDeviceModule
import org.webrtc.audio.JavaAudioDeviceModule
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume

class WebRtcTransport(private val context: Context, private val nativePointer: Long) {

  enum class ConnectionContext(val value: Int) {
    Data(0),
    Media(1);

    companion object {
      fun fromInt(value: Int) = entries.firstOrNull { it.value == value } ?: Data
    }
  }

  private class ConnectionSession(
    val peerId: String,
    val context: ConnectionContext,
    val pc: PeerConnection
  ) {
    var dataChannel: DataChannel? = null
    var iceGatheringComplete: CompletableDeferred<Unit>? = null
    @Volatile var isDisposed = false
    val dataChannelOpenReported = AtomicBoolean(false)

    fun dispose() {
      if (isDisposed) return
      isDisposed = true
      try {
        dataChannel?.unregisterObserver()
        dataChannel?.dispose()
      } catch (_: Exception) {}
      try {
        pc.close()
      } catch (_: Exception) {}
    }
  }

  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private val eglBase: EglBase = Companion.eglBase
  private var signalingClient = SignalingClient()

  private var peerConnectionFactory: PeerConnectionFactory? = null
  private var audioDeviceModule: AudioDeviceModule? = null

  // Key: "$peerId:${context.value}"
  private val sessions = ConcurrentHashMap<String, ConnectionSession>()

  // Local Media Resources
  private var localVideoTrack: VideoTrack? = null
  private var localAudioTrack: AudioTrack? = null
  private var videoCapturer: VideoCapturer? = null
  private var surfaceTextureHelper: SurfaceTextureHelper? = null
  private var videoSource: VideoSource? = null
  private var audioSource: org.webrtc.AudioSource? = null
  private var localSink: VideoSink? = null

  private val remoteSinks = ConcurrentHashMap<String, VideoSink>()
  private val remoteVideoTracks = ConcurrentHashMap<String, VideoTrack>()

  private var localPeerId: String? = null
  private var currentSignalingUrl: String = ""
  private var isObservingMessages = false
  private var isObservingSignalingState = false

  private var currentIceServers: List<PeerConnection.IceServer> = listOf(
    PeerConnection.IceServer.builder("stun:stun.relay.metered.ca:80").createIceServer(),
    PeerConnection.IceServer.builder("stun:stun.l.google.com:19302").createIceServer()
  )

  var onError: ((String) -> Unit)? = null

  private external fun nativeOnPeerMessage(nativePointer: Long, message: String)
  private external fun nativeOnSystemMessage(nativePointer: Long, type: String, message: String)
  private external fun nativeOnSignalingError(nativePointer: Long, peerId: String, error: String)
  private external fun nativeOnDataChannelMessage(nativePointer: Long, peerId: String, message: String)
  private external fun nativeOnDataChannelMessageBinary(nativePointer: Long, peerId: String, data: ByteArray)
  private external fun nativeOnDataChannelOpen(nativePointer: Long, peerId: String)
  private external fun nativeOnPeerConnectionStateChange(nativePointer: Long, peerId: String, context: Int, state: Int)
  private external fun nativeOnSignalingConnectionStateChange(nativePointer: Long, state: Int)

  init {
    initializeFactory()
  }

  private fun getSessionKey(peerId: String, context: ConnectionContext): String {
    return "$peerId:${context.value}"
  }

  private fun initializeFactory() {
    if (peerConnectionFactory != null) return
    PeerConnectionFactory.initialize(
      PeerConnectionFactory.InitializationOptions.builder(context)
        .setEnableInternalTracer(false)
        .createInitializationOptions()
    )

    audioDeviceModule = JavaAudioDeviceModule.builder(context)
      .setUseHardwareAcousticEchoCanceler(true)
      .setUseHardwareNoiseSuppressor(true)
      .createAudioDeviceModule()

    val videoEncoderFactory = DefaultVideoEncoderFactory(eglBase.eglBaseContext, true, true)
    val videoDecoderFactory = DefaultVideoDecoderFactory(eglBase.eglBaseContext)
    peerConnectionFactory = PeerConnectionFactory.builder()
      .setAudioDeviceModule(audioDeviceModule)
      .setVideoEncoderFactory(videoEncoderFactory)
      .setVideoDecoderFactory(videoDecoderFactory)
      .createPeerConnectionFactory()
  }

  fun connect(peerId: String, signalingUrl: String) {
    initializeFactory()
    this.localPeerId = peerId
    this.currentSignalingUrl = signalingUrl
    observeSignalingMessages()
    observeSignalingState()
    signalingClient.connect(peerId, signalingUrl)
  }

  fun terminate() {
    signalingClient.disconnect()
    stopLocalMedia()
    sessions.values.forEach { it.dispose() }
    sessions.clear()
    peerConnectionFactory?.dispose()
    peerConnectionFactory = null

    audioDeviceModule?.release()
    audioDeviceModule = null
  }

  @Suppress("unused")
  fun reconnect(newUrl: String) {
    scope.launch {
      localPeerId?.let { peerId ->
        currentSignalingUrl = newUrl
        signalingClient.disconnect()
        signalingClient = SignalingClient()
        isObservingMessages = false
        isObservingSignalingState = false
        connect(peerId, newUrl)
      }
    }
  }

  @Suppress("unused")
  fun updateIceServers(jsonConfig: String) {
    try {
      val servers = mutableListOf<PeerConnection.IceServer>()
      val jsonArray = JSONArray(jsonConfig)
      for (i in 0 until jsonArray.length()) {
        val entry = jsonArray.optJSONObject(i) ?: continue
        if (!entry.has("urls")) continue
        val urls = mutableListOf<String>()
        val urlsObj = entry.get("urls")
        if (urlsObj is JSONArray) {
          for (j in 0 until urlsObj.length()) urls.add(urlsObj.getString(j))
        } else {
          urls.add(urlsObj.toString())
        }
        val builder = PeerConnection.IceServer.builder(urls)
        if (entry.has("username")) builder.setUsername(entry.getString("username"))
        if (entry.has("credential")) builder.setPassword(entry.getString("credential"))
        servers.add(builder.createIceServer())
      }
      currentIceServers = servers
    } catch (e: Exception) {
      onError?.invoke("Connection Configuration Error: Failed to parse server settings.")
    }
  }

  @Suppress("unused")
  fun createPeerConnection(peerId: String, contextInt: Int) {
    val context = ConnectionContext.fromInt(contextInt)
    val key = getSessionKey(peerId, context)
    if (sessions.containsKey(key)) return
    val factory = peerConnectionFactory ?: return

    nativeOnPeerConnectionStateChange(nativePointer, peerId, context.value, PeerConnectionState.New.value)

    val rtcConfig = RTCConfiguration(currentIceServers).apply {
      sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
      iceTransportsType = PeerConnection.IceTransportsType.ALL
      bundlePolicy = PeerConnection.BundlePolicy.MAXBUNDLE
      rtcpMuxPolicy = PeerConnection.RtcpMuxPolicy.REQUIRE
      continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_ONCE
    }

    val observer = object : PeerConnection.Observer {
      override fun onSignalingChange(state: PeerConnection.SignalingState?) {}
      override fun onConnectionChange(newState: PeerConnection.PeerConnectionState?) {
        val stateInt = when (newState) {
          PeerConnection.PeerConnectionState.NEW -> PeerConnectionState.New.value
          PeerConnection.PeerConnectionState.CONNECTING -> PeerConnectionState.Connecting.value
          PeerConnection.PeerConnectionState.CONNECTED -> PeerConnectionState.Connected.value
          PeerConnection.PeerConnectionState.DISCONNECTED -> PeerConnectionState.Disconnected.value
          PeerConnection.PeerConnectionState.FAILED -> PeerConnectionState.Failed.value
          PeerConnection.PeerConnectionState.CLOSED -> PeerConnectionState.Closed.value
          null -> PeerConnectionState.Closed.value
        }
        nativeOnPeerConnectionStateChange(nativePointer, peerId, context.value, stateInt)
      }
      override fun onIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onStandardizedIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onIceConnectionReceivingChange(receiving: Boolean) {}
      override fun onIceGatheringChange(state: PeerConnection.IceGatheringState?) {
        if (state == PeerConnection.IceGatheringState.COMPLETE) {
          sessions[key]?.iceGatheringComplete?.complete(Unit)
        }
      }
      override fun onIceCandidate(candidate: IceCandidate?) {}
      override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}
      override fun onAddStream(stream: MediaStream?) {}
      override fun onRemoveStream(stream: MediaStream?) {}
      override fun onRenegotiationNeeded() {}
      override fun onDataChannel(dc: DataChannel?) {
        dc ?: return
        val session = sessions[key] ?: return
        if (context == ConnectionContext.Data) {
          setupDataChannel(peerId, dc, session)
        }
      }
      override fun onTrack(transceiver: RtpTransceiver?) {}
      override fun onAddTrack(receiver: RtpReceiver, streams: Array<out MediaStream>) {
        receiver.track()?.let { track ->
          if (track is VideoTrack) {
            remoteVideoTracks[peerId] = track
            remoteSinks[peerId]?.let { sink -> track.addSink(sink) }
          } else if (track is AudioTrack) {
            track.setEnabled(true)
          }
        }
      }
    }

    val pc = factory.createPeerConnection(rtcConfig, observer) ?: return
    val session = ConnectionSession(peerId, context, pc)
    sessions[key] = session
  }

  @Suppress("unused")
  fun createOffer(peerId: String, contextInt: Int) {
    val context = ConnectionContext.fromInt(contextInt)
    val key = getSessionKey(peerId, context)
    val session = sessions[key] ?: return
    scope.launch {
      session.iceGatheringComplete = CompletableDeferred()

      if (session.context == ConnectionContext.Media) {
        attachLocalMedia(session)
      }

      val constraints = MediaConstraints()
      val offerSdp = suspendCancellableCoroutine<SessionDescription?> { cont ->
        session.pc.createOffer(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) = cont.resume(sdp)
          override fun onSetSuccess() {}
          override fun onCreateFailure(s: String?) = cont.resume(null)
          override fun onSetFailure(s: String?) {}
        }, constraints)
      } ?: return@launch

      val setSuccess = suspendCancellableCoroutine<Boolean> { cont ->
        session.pc.setLocalDescription(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) {}
          override fun onSetSuccess() = cont.resume(true)
          override fun onCreateFailure(s: String?) = cont.resume(false)
          override fun onSetFailure(s: String?) = cont.resume(false)
        }, offerSdp)
      }

      if (setSuccess) {
        withTimeoutOrNull(GATHERING_TIMEOUT_MS) { session.iceGatheringComplete?.await() }
        val finalSdp = session.pc.localDescription ?: offerSdp
        val message = JSONObject().apply {
          put("type", "offer")
          put("sdp", finalSdp.description)
          put("context", if (session.context == ConnectionContext.Media) "media" else "data")
          put("to", peerId)
        }
        sendSignalingMessage(message.toString())
      }
    }
  }

  @Suppress("unused")
  fun createAnswer(peerId: String, contextInt: Int) {
    val context = ConnectionContext.fromInt(contextInt)
    val key = getSessionKey(peerId, context)
    val session = sessions[key] ?: return
    scope.launch {
      session.iceGatheringComplete = CompletableDeferred()

      if (session.context == ConnectionContext.Media) {
        attachLocalMedia(session)
      }

      val constraints = MediaConstraints().apply {
        mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveAudio", "true"))
        mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveVideo", "true"))
      }

      val answerSdp = suspendCancellableCoroutine<SessionDescription?> { cont ->
        session.pc.createAnswer(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) = cont.resume(sdp)
          override fun onSetSuccess() {}
          override fun onCreateFailure(s: String?) = cont.resume(null)
          override fun onSetFailure(s: String?) {}
        }, constraints)
      } ?: return@launch

      val setSuccess = suspendCancellableCoroutine<Boolean> { cont ->
        session.pc.setLocalDescription(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) {}
          override fun onSetSuccess() = cont.resume(true)
          override fun onCreateFailure(s: String?) = cont.resume(false)
          override fun onSetFailure(s: String?) = cont.resume(false)
        }, answerSdp)
      }

      if (setSuccess) {
        withTimeoutOrNull(GATHERING_TIMEOUT_MS) { session.iceGatheringComplete?.await() }
        val finalSdp = session.pc.localDescription ?: answerSdp
        val message = JSONObject().apply {
          put("type", "answer")
          put("sdp", finalSdp.description)
          put("context", if (session.context == ConnectionContext.Media) "media" else "data")
          put("to", peerId)
        }
        sendSignalingMessage(message.toString())
      }
    }
  }

  fun setRemoteDescription(peerId: String, contextInt: Int, sdp: String, type: String) {
    val context = ConnectionContext.fromInt(contextInt)
    val key = getSessionKey(peerId, context)
    val session = sessions[key] ?: return
    if (session.isDisposed || sdp.isBlank()) return

    val canonicalType = when (type.lowercase()) {
      "offer" -> SessionDescription.Type.OFFER
      "answer" -> SessionDescription.Type.ANSWER
      "pranswer" -> SessionDescription.Type.PRANSWER
      else -> return
    }

    session.pc.setRemoteDescription(object : SdpObserver {
      override fun onCreateSuccess(sdp: SessionDescription?) {}
      override fun onSetSuccess() {}
      override fun onCreateFailure(s: String?) {}
      override fun onSetFailure(s: String?) {}
    }, SessionDescription(canonicalType, sdp))
  }

  @Suppress("unused")
  fun createDataChannel(peerId: String) {
    val key = getSessionKey(peerId, ConnectionContext.Data)
    val session = sessions[key] ?: return
    val init = DataChannel.Init()
    val dc = session.pc.createDataChannel("data", init)
    setupDataChannel(peerId, dc, session)
  }

  private fun setupDataChannel(peerId: String, dc: DataChannel, session: ConnectionSession) {
    session.dataChannel = dc
    dc.registerObserver(object : DataChannel.Observer {
      override fun onBufferedAmountChange(amount: Long) {}
      override fun onStateChange() {
        Log.i(TAG, "DataChannel state changed: ${dc.state()} for peer $peerId")
        if (dc.state() == DataChannel.State.OPEN) {
          if (session.dataChannelOpenReported.compareAndSet(false, true)) {
            nativeOnDataChannelOpen(nativePointer, peerId)
          }
        }
      }
      override fun onMessage(buffer: DataChannel.Buffer) {
        val data = ByteArray(buffer.data.remaining())
        buffer.data.get(data)
        if (buffer.binary) {
          nativeOnDataChannelMessageBinary(nativePointer, peerId, data)
        } else {
          nativeOnDataChannelMessage(nativePointer, peerId, String(data))
        }
      }
    })
    if (dc.state() == DataChannel.State.OPEN) {
      if (session.dataChannelOpenReported.compareAndSet(false, true)) {
        nativeOnDataChannelOpen(nativePointer, peerId)
      }
    }
  }

  private fun attachLocalMedia(session: ConnectionSession) {
    val senders = session.pc.senders
    localAudioTrack?.let { track ->
      if (senders.none { it.track()?.id() == AUDIO_TRACK_ID }) {
        session.pc.addTrack(track, listOf(MEDIA_STREAM_ID))
      }
    }
    localVideoTrack?.let { track ->
      if (senders.none { it.track()?.id() == VIDEO_TRACK_ID }) {
        session.pc.addTrack(track, listOf(MEDIA_STREAM_ID))
      }
    }
  }

  fun startLocalMedia(isVideo: Boolean) {
    if (peerConnectionFactory == null) initializeFactory()
    try {
      if (localAudioTrack == null) {
        val audioConstraints = MediaConstraints()
        audioSource = peerConnectionFactory!!.createAudioSource(audioConstraints)
        localAudioTrack = peerConnectionFactory!!.createAudioTrack(AUDIO_TRACK_ID, audioSource)
      }
      if (isVideo && localVideoTrack == null) {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return
        val enumerator = Camera2Enumerator(context)
        val deviceName = enumerator.deviceNames.firstOrNull { enumerator.isFrontFacing(it) }
          ?: enumerator.deviceNames.firstOrNull()
        if (deviceName != null) {
          videoCapturer = enumerator.createCapturer(deviceName, null)
          val source = peerConnectionFactory!!.createVideoSource(videoCapturer!!.isScreencast)
          videoSource = source
          surfaceTextureHelper = SurfaceTextureHelper.create("CaptureThread", eglBase.eglBaseContext)
          videoCapturer!!.initialize(surfaceTextureHelper, context, source.capturerObserver)
          videoCapturer!!.startCapture(640, 480, 30)
          localVideoTrack = peerConnectionFactory!!.createVideoTrack(VIDEO_TRACK_ID, source)
          localSink?.let { localVideoTrack?.addSink(it) }
        }
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to start local media", e)
    }
  }

  fun stopLocalMedia() {
    sessions.values.forEach { session ->
      if (!session.isDisposed && session.context == ConnectionContext.Media) {
        try {
          val senders = session.pc.senders
          for (sender in senders) {
            val track = sender.track()
            if (track != null && (track.id() == AUDIO_TRACK_ID || track.id() == VIDEO_TRACK_ID)) {
              track.setEnabled(false)
              session.pc.removeTrack(sender)
            }
          }
        } catch (e: Exception) {
          Log.w(TAG, "Error removing tracks from session ${session.peerId}", e)
        }
      }
    }

    localVideoTrack?.let {
      it.setEnabled(false)
      localSink?.let { sink -> it.removeSink(sink) }
      it.dispose()
    }
    localVideoTrack = null

    try {
      videoCapturer?.stopCapture()
    } catch (e: Exception) {
      Log.w(TAG, "Error stopping video capture", e)
    }
    videoCapturer?.dispose()
    videoCapturer = null

    surfaceTextureHelper?.dispose()
    surfaceTextureHelper = null

    videoSource?.dispose()
    videoSource = null

    localAudioTrack?.let {
      it.setEnabled(false)
      it.dispose()
    }
    localAudioTrack = null

    audioSource?.dispose()
    audioSource = null
  }

  fun addVideoTrack(peerId: String, contextInt: Int) {
    val context = ConnectionContext.fromInt(contextInt)
    val key = getSessionKey(peerId, context)
    sessions[key]?.let { attachLocalMedia(it) }
  }

  fun addAudioTrack(peerId: String, contextInt: Int) {
    val context = ConnectionContext.fromInt(contextInt)
    val key = getSessionKey(peerId, context)
    sessions[key]?.let { attachLocalMedia(it) }
  }

  fun setLocalSink(sink: VideoSink?) {
    localSink?.let { localVideoTrack?.removeSink(it) }
    localSink = sink
    localSink?.let { localVideoTrack?.addSink(it) }
  }

  fun setRemoteSink(peerId: String, sink: VideoSink?) {
    val track = remoteVideoTracks[peerId] ?: return
    val oldSink = remoteSinks[peerId]
    if (oldSink != null) track.removeSink(oldSink)
    if (sink != null) {
      remoteSinks[peerId] = sink
      track.addSink(sink)
    } else {
      remoteSinks.remove(peerId)
    }
  }

  fun closePeerConnection(peerId: String, contextInt: Int) {
    val context = ConnectionContext.fromInt(contextInt)
    val key = getSessionKey(peerId, context)
    sessions.remove(key)?.dispose()

    if (context == ConnectionContext.Media) {
      remoteVideoTracks.remove(peerId)
      remoteSinks.remove(peerId)
    }
  }

  fun sendSignalingMessage(message: String) {
    signalingClient.send(JSONObject(message))
  }

  @Suppress("unused")
  fun sendDataChannelMessage(peerId: String, message: String): Boolean {
    // We send data channel messages only over the 'data' context connection
    val key = getSessionKey(peerId, ConnectionContext.Data)
    val session = sessions[key]
    if (session == null) {
      Log.w(TAG, "sendDataChannelMessage: No session found for $key")
      return false
    }
    val dc = session.dataChannel ?: return false

    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(message.toByteArray())
      return dc.send(DataChannel.Buffer(buffer, false))
    }
    Log.w(TAG, "sendDataChannelMessage: DataChannel for $key is in state ${dc.state()}")
    return false
  }

  @Suppress("unused")
  fun sendDataChannelMessageBinary(peerId: String, data: ByteArray): Boolean {
    val key = getSessionKey(peerId, ConnectionContext.Data)
    val session = sessions[key]
    if (session == null) {
      Log.w(TAG, "sendDataChannelMessageBinary: No session found for $key")
      return false
    }
    val dc = session.dataChannel ?: return false

    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(data)
      return dc.send(DataChannel.Buffer(buffer, true))
    }
    Log.w(TAG, "sendDataChannelMessageBinary: DataChannel for $key is in state ${dc.state()}")
    return false
  }

  @Suppress("unused")
  fun getPeerConnectionState(peerId: String): Int {
    val key = getSessionKey(peerId, ConnectionContext.Data)
    val state = sessions[key]?.pc?.connectionState()
    return when (state) {
      PeerConnection.PeerConnectionState.NEW -> PeerConnectionState.New.ordinal
      PeerConnection.PeerConnectionState.CONNECTING -> PeerConnectionState.Connecting.ordinal
      PeerConnection.PeerConnectionState.CONNECTED -> PeerConnectionState.Connected.ordinal
      PeerConnection.PeerConnectionState.DISCONNECTED -> PeerConnectionState.Disconnected.ordinal
      PeerConnection.PeerConnectionState.FAILED -> PeerConnectionState.Failed.ordinal
      PeerConnection.PeerConnectionState.CLOSED -> PeerConnectionState.Closed.ordinal
      else -> PeerConnectionState.Closed.ordinal
    }
  }

  private fun observeSignalingMessages() {
    if (isObservingMessages) return
    isObservingMessages = true
    signalingClient.messages.onEach { message ->
      scope.launch {
        when (message) {
          is SignalingMessage.PeerMessage -> nativeOnPeerMessage(nativePointer, message.message.toString())
          is SignalingMessage.Error -> nativeOnSignalingError(nativePointer, message.forPeer ?: "", message.error)
          is SignalingMessage.SystemMessage -> nativeOnSystemMessage(nativePointer, message.type, message.payload.toString())
        }
      }
    }.launchIn(scope)
  }

  private fun observeSignalingState() {
    if (isObservingSignalingState) return
    isObservingSignalingState = true
    signalingClient.state.onEach { state ->
      scope.launch { nativeOnSignalingConnectionStateChange(nativePointer, state.ordinal) }
    }.launchIn(scope)
  }

  companion object {
    private const val TAG = "WebRtcTransport"
    private const val MEDIA_STREAM_ID = "ARDAMS"
    private const val VIDEO_TRACK_ID = "ARDAMSv0"
    private const val AUDIO_TRACK_ID = "ARDAMSa0"
    private const val GATHERING_TIMEOUT_MS = 1500L
    val eglBase: EglBase by lazy { EglBase.create() }
  }
}
