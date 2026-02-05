package hela.eillance.webrtc

import android.content.Context
import android.util.Log
import hela.eillance.ndkio.PeerConnectionState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONException
import org.json.JSONObject
import org.webrtc.AudioSource
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
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap
import java.util.regex.Pattern

/**
 * Encapsulates the lifecycle of a single PeerConnection attempt.
 * Ensures the timeout job is strictly bound to this specific instance.
 */
private class ConnectionSession(
  val peerId: String,
  val context: String,
  val pc: PeerConnection,
  private val scope: CoroutineScope,
  private val onTimeout: (ConnectionSession) -> Unit
) {
  var dataChannel: DataChannel? = null
  private var timeoutJob: Job? = null
  private var isDisposed = false

  fun startTimeout(ms: Long) {
    timeoutJob = scope.launch {
      delay(ms)
      synchronized(this@ConnectionSession) {
        if (isDisposed) return@launch
        val state = pc.connectionState()
        // FIX: Also check if DataChannel is OPEN. If data is flowing, we are connected,
        // regardless of what the ICE/DTLS state machine reports.
        val isDataChannelOpen = dataChannel?.state() == DataChannel.State.OPEN

        if (state != PeerConnection.PeerConnectionState.CONNECTED &&
          state != PeerConnection.PeerConnectionState.CLOSED &&
          !isDataChannelOpen
        ) {
          Log.w("WebRtcTransport", "Session $peerId ($context) timed out. State: $state. Closing.")
          onTimeout(this@ConnectionSession)
        }
      }
    }
  }

  fun cancelTimeout() {
    timeoutJob?.cancel()
    timeoutJob = null
  }

  fun dispose() {
    synchronized(this) {
      if (isDisposed) return
      isDisposed = true
    }
    cancelTimeout()
    scope.launch(Dispatchers.IO) {
      try {
        dataChannel?.unregisterObserver()
        dataChannel?.dispose()
      } catch (e: Exception) {
        Log.w("WebRtcTransport", "Error disposing DataChannel", e)
      }
      try {
        pc.close()
      } catch (e: Exception) {
        Log.w("WebRtcTransport", "Error closing PeerConnection", e)
      }
    }
  }
}

class WebRtcTransport(private val context: Context, private val nativePointer: Long) {
  private var coroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private val eglBase: EglBase = WebRtcTransport.eglBase
  private var signalingClient = SignalingClient()

  private var peerConnectionFactory: PeerConnectionFactory? = null

  // Map of active sessions. Replacing the inner map value automatically orphans the old session.
  private val sessions = ConcurrentHashMap<String, ConcurrentHashMap<String, ConnectionSession>>()

  // Local media resources
  private var localVideoTrack: VideoTrack? = null
  private var localAudioTrack: AudioTrack? = null
  private var videoCapturer: VideoCapturer? = null
  private var videoSource: VideoSource? = null
  private var audioSource: AudioSource? = null

  // Sinks for rendering video
  private var localSink: VideoSink? = null
  private val remoteSinks = ConcurrentHashMap<String, VideoSink>()
  private val remoteVideoTracks = ConcurrentHashMap<String, VideoTrack>()

  private var localPeerId: String? = null
  private var currentSignalingUrl: String = ""

  private var isObservingMessages = false
  private var isObservingSignalingState = false

  private external fun nativeOnPeerMessage(nativePointer: Long, message: String)
  private external fun nativeOnSystemMessage(nativePointer: Long, type: String, message: String)
  private external fun nativeOnSignalingError(nativePointer: Long, peerId: String, error: String)
  private external fun nativeOnDataChannelMessage(nativePointer: Long, peerId: String, message: String)
  private external fun nativeOnDataChannelMessageBinary(nativePointer: Long, peerId: String, data: ByteArray)
  private external fun nativeOnDataChannelOpen(nativePointer: Long, peerId: String)
  private external fun nativeOnPeerConnectionStateChange(nativePointer: Long, peerId: String, state: Int)
  private external fun nativeOnSignalingConnectionStateChange(nativePointer: Long, state: Int)

  init {
    initialize()
  }

  private fun initialize() {
    if (peerConnectionFactory != null) return

    if (!coroutineScope.isActive) {
      coroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    }

    Log.d(TAG, "Initializing PeerConnectionFactory")
    PeerConnectionFactory.initialize(
      PeerConnectionFactory.InitializationOptions.builder(context).createInitializationOptions()
    )
    val videoEncoderFactory = DefaultVideoEncoderFactory(eglBase.eglBaseContext, true, true)
    val videoDecoderFactory = DefaultVideoDecoderFactory(eglBase.eglBaseContext)
    peerConnectionFactory = PeerConnectionFactory.builder()
      .setVideoEncoderFactory(videoEncoderFactory)
      .setVideoDecoderFactory(videoDecoderFactory)
      .createPeerConnectionFactory()
  }

  fun connect(peerId: String, signalingUrl: String) {
    initialize()
    this.localPeerId = peerId
    this.currentSignalingUrl = signalingUrl
    observeSignalingMessages()
    observeSignalingState()
    signalingClient.connect(peerId, signalingUrl)
  }

  fun terminate() {
    signalingClient.disconnect()
    stopLocalMedia()
    sessions.values.forEach { contextMap ->
      contextMap.values.forEach { session ->
        session.dispose()
      }
    }
    sessions.clear()
    peerConnectionFactory?.dispose()
    peerConnectionFactory = null
  }

  fun createPeerConnection(peerId: String, context: String) {
    val factory = peerConnectionFactory
    if (factory == null) {
      Log.e(TAG, "PeerConnectionFactory is not initialized")
      return
    }

    // Prepare the map entry
    val peerContextMap = sessions.getOrPut(peerId) { ConcurrentHashMap() }

    // Clean up any existing session for this context.
    peerContextMap.remove(context)?.dispose()

    val iceServers = listOf(
      PeerConnection.IceServer.builder("stun:stun.relay.metered.ca:80").createIceServer(),
      PeerConnection.IceServer.builder("turn:a.relay.metered.ca:80")
        .setUsername("49880a0f046c8990b1990b35")
        .setPassword("qZCNW/57EB8GwO3A")
        .createIceServer(),
      PeerConnection.IceServer.builder("turn:a.relay.metered.ca:443")
        .setUsername("49880a0f046c8990b1990b35")
        .setPassword("qZCNW/57EB8GwO3A")
        .createIceServer()
    )

    val rtcConfig = RTCConfiguration(iceServers).apply {
      sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
      iceTransportsType = PeerConnection.IceTransportsType.ALL
    }

    // Mutable holder for the session to be captured by the observer
    var createdSession: ConnectionSession? = null

    val observer = object : PeerConnection.Observer {
      private fun isCurrentSession(): Boolean {
        val activeSession = sessions[peerId]?.get(context)
        return activeSession != null && activeSession === createdSession
      }

      override fun onSignalingChange(newState: PeerConnection.SignalingState?) {
        if (!isCurrentSession()) return
        Log.i(TAG, "onSignalingChange for peer $peerId ($context): $newState")
      }

      override fun onConnectionChange(newState: PeerConnection.PeerConnectionState?) {
        if (!isCurrentSession()) {
          Log.w(TAG, "Ignoring onConnectionChange ($newState) for ghost session $peerId ($context)")
          return
        }

        Log.i(TAG, "onConnectionChange for peer $peerId ($context): $newState")

        if (newState == PeerConnection.PeerConnectionState.CONNECTED ||
          newState == PeerConnection.PeerConnectionState.FAILED ||
          newState == PeerConnection.PeerConnectionState.CLOSED ||
          newState == PeerConnection.PeerConnectionState.DISCONNECTED
        ) {
          createdSession?.cancelTimeout()
        }

        if (context == "data") {
          val stateInt = when (newState) {
            PeerConnection.PeerConnectionState.NEW -> PeerConnectionState.New.value
            PeerConnection.PeerConnectionState.CONNECTING -> PeerConnectionState.Connecting.value
            PeerConnection.PeerConnectionState.CONNECTED -> PeerConnectionState.Connected.value
            PeerConnection.PeerConnectionState.DISCONNECTED -> PeerConnectionState.Disconnected.value
            PeerConnection.PeerConnectionState.FAILED -> PeerConnectionState.Failed.value
            PeerConnection.PeerConnectionState.CLOSED -> PeerConnectionState.Closed.value
            null -> PeerConnectionState.Closed.value
          }
          nativeOnPeerConnectionStateChange(nativePointer, peerId, stateInt)
        }
      }

      override fun onStandardizedIceConnectionChange(newState: PeerConnection.IceConnectionState?) {}
      override fun onIceConnectionChange(newState: PeerConnection.IceConnectionState?) {}
      override fun onIceConnectionReceivingChange(p0: Boolean) {}
      override fun onIceGatheringChange(newState: PeerConnection.IceGatheringState?) {
        if (!isCurrentSession()) return
        Log.i(TAG, "onIceGatheringChange for peer $peerId ($context): $newState")
      }

      override fun onIceCandidate(candidate: IceCandidate?) {
        if (!isCurrentSession()) return
        if (candidate == null) {
          Log.d(TAG, "End of ICE candidates for peer $peerId ($context)")
          return
        }
        try {
          val candidateJson = JSONObject().apply {
            put("type", "candidate")
            put("to", peerId)
            put("context", context)
            put("candidate", candidate.sdp)
            put("sdpMLineIndex", candidate.sdpMLineIndex)
            put("sdpMid", candidate.sdpMid)
          }
          sendSignalingMessage(candidateJson.toString())
        } catch (e: JSONException) {
          Log.e(TAG, "Failed to create ICE candidate JSON", e)
        }
      }

      override fun onIceCandidatesRemoved(p0: Array<out IceCandidate>?) {}
      override fun onAddStream(stream: MediaStream?) {}
      override fun onRemoveStream(p0: MediaStream?) {}

      override fun onDataChannel(dc: DataChannel?) {
        if (!isCurrentSession()) return
        dc ?: return
        Log.i(TAG, "onDataChannel received for label: ${dc.label()} from $peerId")

        createdSession?.dataChannel = dc

        val dataChannelObserver = object : DataChannel.Observer {
          override fun onBufferedAmountChange(p0: Long) {}
          override fun onStateChange() {
            Log.i(TAG, "Incoming data channel state for $peerId changed to: ${dc.state()}")
            if (dc.state() == DataChannel.State.OPEN) {
              createdSession?.cancelTimeout() // Ensure timeout is cancelled on DC Open
              nativeOnDataChannelOpen(nativePointer, peerId)
            }
          }

          override fun onMessage(buffer: DataChannel.Buffer?) {
            buffer ?: return
            val data = ByteArray(buffer.data.remaining())
            buffer.data.get(data)
            nativeOnDataChannelMessageBinary(nativePointer, peerId, data)
          }
        }
        dc.registerObserver(dataChannelObserver)

        if (dc.state() == DataChannel.State.OPEN) {
          createdSession?.cancelTimeout()
          nativeOnDataChannelOpen(nativePointer, peerId)
        }
      }

      override fun onRenegotiationNeeded() {
        if (!isCurrentSession()) return
        Log.i(TAG, "onRenegotiationNeeded for peer: $peerId, context: $context")
      }

      override fun onTrack(transceiver: RtpTransceiver?) {}

      override fun onAddTrack(rtpReceiver: RtpReceiver, mediaStreams: Array<out MediaStream>) {
        if (!isCurrentSession()) return
        rtpReceiver.track()?.let { track ->
          Log.i(TAG, "onAddTrack: Received ${track.kind()} track from $peerId")
          when (track) {
            is VideoTrack -> {
              remoteVideoTracks[peerId] = track
              remoteSinks[peerId]?.let { sink ->
                track.addSink(sink)
              }
            }
            is AudioTrack -> {
              track.setEnabled(true)
            }
          }
        }
      }
    }

    Log.i(TAG, "Creating PeerConnection for peer $peerId, context: $context")
    val pc = factory.createPeerConnection(rtcConfig, observer) ?: return

    val session = ConnectionSession(peerId, context, pc, coroutineScope) { s ->
      s.dispose()
    }

    createdSession = session
    peerContextMap[context] = session

    session.startTimeout(CONNECTION_TIMEOUT_MS)
  }

  fun closePeerConnection(peerId: String, context: String) {
    Log.i(TAG, "Closing peer connection for peer $peerId ($context)")
    sessions[peerId]?.remove(context)?.dispose()

    if (sessions[peerId]?.isEmpty() == true) {
      sessions.remove(peerId)
    }

    if (context == "media") {
      remoteVideoTracks.remove(peerId)?.dispose()
      remoteSinks.remove(peerId)
    }
  }

  fun createDataChannel(peerId: String) {
    val session = getSession(peerId, "data") ?: return
    Log.d(TAG, "Creating data channel for peer: $peerId")
    val dc = session.pc.createDataChannel("data", DataChannel.Init())
    session.dataChannel = dc

    val dataChannelObserver = object : DataChannel.Observer {
      override fun onBufferedAmountChange(p0: Long) {}
      override fun onStateChange() {
        Log.i(TAG, "Outgoing data channel state for $peerId changed to: ${dc.state()}")
        if (dc.state() == DataChannel.State.OPEN) {
          getSession(peerId, "data")?.cancelTimeout() // Cancel timeout
          nativeOnDataChannelOpen(nativePointer, peerId)
        }
      }

      override fun onMessage(buffer: DataChannel.Buffer?) {
        buffer ?: return
        val data = ByteArray(buffer.data.remaining())
        buffer.data.get(data)
        nativeOnDataChannelMessageBinary(nativePointer, peerId, data)
      }
    }
    dc.registerObserver(dataChannelObserver)

    if (dc.state() == DataChannel.State.OPEN) {
      session.cancelTimeout()
      nativeOnDataChannelOpen(nativePointer, peerId)
    }
  }

  fun sendDataChannelMessage(peerId: String, message: String): Boolean {
    val dc = sessions[peerId]?.get("data")?.dataChannel
    if (dc == null) {
      Log.e(TAG, "DataChannel for peer $peerId not found.")
      return false
    }
    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(message.toByteArray())
      return dc.send(DataChannel.Buffer(buffer, false))
    }
    return false
  }

  fun sendDataChannelMessageBinary(peerId: String, data: ByteArray): Boolean {
    val dc = sessions[peerId]?.get("data")?.dataChannel
    if (dc == null) {
      Log.e(TAG, "DataChannel for peer $peerId not found for binary.")
      return false
    }
    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(data)
      return dc.send(DataChannel.Buffer(buffer, true))
    }
    return false
  }

  fun createOffer(peerId: String, context: String) {
    val session = getSession(peerId, context) ?: return
    val sdpObserver = object : SdpObserver {
      override fun onCreateSuccess(sdp: SessionDescription?) {
        if (sdp != null) session.pc.setLocalDescription(this, sdp)
      }

      override fun onSetSuccess() {
        session.pc.localDescription?.let { localSdp ->
          val message = JSONObject().apply {
            put("type", localSdp.type.canonicalForm())
            put("sdp", localSdp.description)
            put("to", peerId)
            put("context", context)
          }
          sendSignalingMessage(message.toString())
        }
      }

      override fun onCreateFailure(s: String?) {
        Log.e(TAG, "createOffer failed for peer $peerId: $s")
      }

      override fun onSetFailure(s: String?) {
        Log.e(TAG, "setLocalDescription failed for peer $peerId: $s")
      }
    }
    session.pc.createOffer(sdpObserver, MediaConstraints())
  }

  fun createAnswer(peerId: String, context: String) {
    val session = getSession(peerId, context) ?: return

    if (context == "media") {
      if (localVideoTrack == null || localAudioTrack == null) {
        startLocalMedia()
      }
      if (localAudioTrack != null && session.pc.senders.none { it.track()?.id() == AUDIO_TRACK_ID }) {
        session.pc.addTrack(localAudioTrack, listOf(MEDIA_STREAM_ID))
      }
      if (localVideoTrack != null && session.pc.senders.none { it.track()?.id() == VIDEO_TRACK_ID }) {
        session.pc.addTrack(localVideoTrack, listOf(MEDIA_STREAM_ID))
      }
    }

    val sdpObserver = object : SdpObserver {
      override fun onCreateSuccess(sdp: SessionDescription?) {
        if (sdp != null) session.pc.setLocalDescription(this, sdp)
      }

      override fun onSetSuccess() {
        session.pc.localDescription?.let { localSdp ->
          val message = JSONObject().apply {
            put("type", localSdp.type.canonicalForm())
            put("sdp", localSdp.description)
            put("to", peerId)
            put("context", context)
          }
          sendSignalingMessage(message.toString())
        }
      }

      override fun onCreateFailure(s: String?) {
        Log.e(TAG, "createAnswer failed for peer $peerId: $s")
      }

      override fun onSetFailure(s: String?) {
        Log.e(TAG, "setLocalDescription failed for peer $peerId: $s")
      }
    }
    val answerConstraints = MediaConstraints().apply {
      mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveAudio", "true"))
      mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveVideo", "true"))
    }
    session.pc.createAnswer(sdpObserver, answerConstraints)
  }

  fun setRemoteDescription(peerId: String, context: String, sdp: String, type: String) {
    val session = getSession(peerId, context) ?: run {
      return
    }

    if (type == "answer" && session.pc.signalingState() == PeerConnection.SignalingState.STABLE) {
      val currentRemoteSdp = session.pc.remoteDescription?.description ?: ""
      val currentUfrag = extractIceUfrag(currentRemoteSdp)
      val newUfrag = extractIceUfrag(sdp)

      if (currentUfrag != null && newUfrag != null && currentUfrag != newUfrag) {
        Log.e(TAG, "Ghost Answer detected! Ufrag mismatch. Restarting.")
        session.dispose()
        nativeOnPeerConnectionStateChange(nativePointer, peerId, PeerConnectionState.Failed.value)
        return
      } else {
        return
      }
    }

    val sdpObserver = object : SdpObserver {
      override fun onCreateSuccess(sdp: SessionDescription?) {}
      override fun onSetSuccess() {}
      override fun onCreateFailure(s: String?) {}
      override fun onSetFailure(s: String?) {
        Log.e(TAG, "Failed to set remote description for peer $peerId: $s")
      }
    }
    val sdpType = SessionDescription.Type.fromCanonicalForm(type)
    session.pc.setRemoteDescription(sdpObserver, SessionDescription(sdpType, sdp))
  }

  fun addRemoteCandidate(peerId: String, context: String, candidateSdp: String, mid: String) {
    val session = getSession(peerId, context) ?: return
    val candidate = IceCandidate(mid, -1, candidateSdp)
    session.pc.addIceCandidate(candidate)
  }

  fun addVideoTrack(peerId: String) {
    val session = getSession(peerId, "media") ?: return
    if (localVideoTrack == null) startLocalMedia()
    localVideoTrack?.let {
      session.pc.addTrack(it, listOf(MEDIA_STREAM_ID))
    }
  }

  fun addAudioTrack(peerId: String) {
    val session = getSession(peerId, "media") ?: return
    if (localAudioTrack == null) startLocalMedia()
    localAudioTrack?.let {
      session.pc.addTrack(it, listOf(MEDIA_STREAM_ID))
    }
  }

  fun startLocalMedia() {
    val factory = peerConnectionFactory ?: return
    if (localVideoTrack != null || localAudioTrack != null) return

    val cameraEnumerator = Camera2Enumerator(context)
    val frontCamera = cameraEnumerator.deviceNames.firstOrNull { cameraEnumerator.isFrontFacing(it) }

    if (frontCamera != null) {
      videoCapturer = cameraEnumerator.createCapturer(frontCamera, null)
      videoSource = factory.createVideoSource(videoCapturer!!.isScreencast)
      val surfaceTextureHelper = SurfaceTextureHelper.create("VideoCapturerThread", eglBase.eglBaseContext)
      videoCapturer!!.initialize(surfaceTextureHelper, context, videoSource!!.capturerObserver)
      videoCapturer!!.startCapture(480, 640, 30)
      localVideoTrack = factory.createVideoTrack(VIDEO_TRACK_ID, videoSource).apply {
        localSink?.let { addSink(it) }
      }
    }

    val audioConstraints = MediaConstraints()
    audioSource = factory.createAudioSource(audioConstraints)
    localAudioTrack = factory.createAudioTrack(AUDIO_TRACK_ID, audioSource)
  }

  fun stopLocalMedia() {
    localVideoTrack?.let {
      localSink?.let { sink -> it.removeSink(sink) }
      it.dispose()
      localVideoTrack = null
    }
    videoCapturer?.stopCapture()
    videoCapturer?.dispose()
    videoCapturer = null
    videoSource?.dispose()
    videoSource = null
    localAudioTrack?.dispose()
    localAudioTrack = null
    audioSource?.dispose()
    audioSource = null
  }

  fun setLocalSink(sink: VideoSink?) {
    localSink?.let { localVideoTrack?.removeSink(it) }
    localSink = sink
    localSink?.let { localVideoTrack?.addSink(it) }
  }

  fun setRemoteSink(peerId: String, sink: VideoSink?) {
    remoteSinks[peerId]?.let { oldSink ->
      remoteVideoTracks[peerId]?.removeSink(oldSink)
    }
    if (sink == null) {
      remoteSinks.remove(peerId)
    } else {
      remoteSinks[peerId] = sink
      remoteVideoTracks[peerId]?.let { track ->
        track.addSink(sink)
      }
    }
  }

  fun sendSignalingMessage(message: String) {
    signalingClient.send(JSONObject(message))
  }

  fun reconnect(newUrl: String) {
    coroutineScope.launch {
      localPeerId?.let { peerId ->
        Log.i(TAG, "Reconnecting signaling client to new URL: $newUrl")
        this@WebRtcTransport.currentSignalingUrl = newUrl
        signalingClient.disconnect()
        signalingClient = SignalingClient()
        isObservingMessages = false
        isObservingSignalingState = false
        observeSignalingMessages()
        observeSignalingState()
        signalingClient.connect(peerId, newUrl)
      }
    }
  }

  fun getPeerConnectionState(peerId: String): Int {
    val state = sessions[peerId]?.get("data")?.pc?.connectionState()
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

  private fun getSession(peerId: String, context: String): ConnectionSession? {
    return sessions[peerId]?.get(context)
  }

  private fun extractIceUfrag(sdp: String): String? {
    val matcher = Pattern.compile("a=ice-ufrag:(\\S+)").matcher(sdp)
    return if (matcher.find()) matcher.group(1) else null
  }

  private fun observeSignalingMessages() {
    if (isObservingMessages) return
    isObservingMessages = true

    signalingClient.messages.onEach { message ->
      coroutineScope.launch {
        when (message) {
          is SignalingMessage.PeerMessage -> nativeOnPeerMessage(nativePointer, message.message.toString())
          is SignalingMessage.Error -> nativeOnSignalingError(nativePointer, message.forPeer ?: "", message.error)
          is SignalingMessage.SystemMessage -> nativeOnSystemMessage(
            nativePointer,
            message.type,
            message.payload.toString()
          )
        }
      }
    }.launchIn(coroutineScope)
  }

  private fun observeSignalingState() {
    if (isObservingSignalingState) return
    isObservingSignalingState = true

    signalingClient.state.onEach { state ->
      coroutineScope.launch {
        Log.d(TAG, "Signaling client state changed: $state")
        nativeOnSignalingConnectionStateChange(nativePointer, state.ordinal)
      }
    }.launchIn(coroutineScope)
  }

  companion object {
    private const val TAG = "WebRtcTransport"
    private const val MEDIA_STREAM_ID = "ARDAMS"
    private const val VIDEO_TRACK_ID = "ARDAMSv0"
    private const val AUDIO_TRACK_ID = "ARDAMSa0"
    private const val CONNECTION_TIMEOUT_MS = 30_000L

    val eglBase: EglBase by lazy {
      Log.d(TAG, "Creating singleton EglBase.")
      EglBase.create()
    }
  }
}
