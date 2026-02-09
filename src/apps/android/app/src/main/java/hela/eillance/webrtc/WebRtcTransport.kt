package hela.eillance.webrtc

import android.content.Context
import android.util.Log
import hela.eillance.ndkio.PeerConnectionState
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.withTimeoutOrNull
import org.json.JSONArray
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
import kotlin.coroutines.resume

/**
 * Represents a WebRTC session (Data or Media) with a peer.
 */
private class ConnectionSession(
  val peerId: String,
  val context: String,
  val pc: PeerConnection
) {
  var dataChannel: DataChannel? = null

  // Signal to synchronize Vanilla ICE gathering.
  // Using var to allow resetting for renegotiation.
  var iceGatheringComplete: CompletableDeferred<Unit>? = null

  // Used to prevent operations on closed sessions
  @Volatile
  var isDisposed = false

  fun dispose() {
    if (isDisposed) return
    isDisposed = true

    try {
      dataChannel?.unregisterObserver()
      dataChannel?.dispose()
    } catch (e: Exception) {
      Log.w("WebRtcTransport", "Error disposing DataChannel for $peerId ($context)", e)
    }
    try {
      pc.close()
    } catch (e: Exception) {
      Log.w("WebRtcTransport", "Error closing PeerConnection for $peerId ($context)", e)
    }
  }
}

class WebRtcTransport(private val context: Context, private val nativePointer: Long) {
  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private val eglBase: EglBase = Companion.eglBase
  private var signalingClient = SignalingClient()

  private var peerConnectionFactory: PeerConnectionFactory? = null

  // Thread-safe session management
  private val sessions = ConcurrentHashMap<String, ConcurrentHashMap<String, ConnectionSession>>()

  // Mutex for synchronizing access to peer connection creation/deletion
  private val sessionMutex = Mutex()

  // Local Media Resources
  private var localVideoTrack: VideoTrack? = null
  private var localAudioTrack: AudioTrack? = null
  private var videoCapturer: VideoCapturer? = null
  private var videoSource: VideoSource? = null
  private var audioSource: AudioSource? = null
  private var localSink: VideoSink? = null

  private val remoteSinks = ConcurrentHashMap<String, VideoSink>()
  private val remoteVideoTracks = ConcurrentHashMap<String, VideoTrack>()

  private var localPeerId: String? = null
  private var currentSignalingUrl: String = ""
  private var isObservingMessages = false
  private var isObservingSignalingState = false

  // ICE Servers config
  private var currentIceServers: List<PeerConnection.IceServer> = listOf(
    PeerConnection.IceServer.builder("stun:stun.relay.metered.ca:80").createIceServer(),
    PeerConnection.IceServer.builder("stun:stun.l.google.com:19302").createIceServer()
  )

  // Callback to report non-fatal errors to UI/Service
  var onError: ((String) -> Unit)? = null

  // JNI Callbacks
  private external fun nativeOnPeerMessage(nativePointer: Long, message: String)
  private external fun nativeOnSystemMessage(nativePointer: Long, type: String, message: String)
  private external fun nativeOnSignalingError(nativePointer: Long, peerId: String, error: String)
  private external fun nativeOnDataChannelMessage(nativePointer: Long, peerId: String, message: String)
  private external fun nativeOnDataChannelMessageBinary(nativePointer: Long, peerId: String, data: ByteArray)
  private external fun nativeOnDataChannelOpen(nativePointer: Long, peerId: String)
  private external fun nativeOnPeerConnectionStateChange(nativePointer: Long, peerId: String, state: Int)
  private external fun nativeOnSignalingConnectionStateChange(nativePointer: Long, state: Int)

  init {
    initializeFactory()
  }

  private fun initializeFactory() {
    if (peerConnectionFactory != null) return
    Log.d(TAG, "Initializing PeerConnectionFactory")

    PeerConnectionFactory.initialize(
      PeerConnectionFactory.InitializationOptions.builder(context)
        .setEnableInternalTracer(false)
        .createInitializationOptions()
    )

    val videoEncoderFactory = DefaultVideoEncoderFactory(eglBase.eglBaseContext, true, true)
    val videoDecoderFactory = DefaultVideoDecoderFactory(eglBase.eglBaseContext)

    peerConnectionFactory = PeerConnectionFactory.builder()
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
    Log.i(TAG, "Terminating WebRtcTransport")
    signalingClient.disconnect()
    stopLocalMedia()

    sessions.values.forEach { contextMap ->
      contextMap.values.forEach { it.dispose() }
    }
    sessions.clear()

    peerConnectionFactory?.dispose()
    peerConnectionFactory = null
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
        val entry = jsonArray.optJSONObject(i)
        if (entry == null) {
          // Skip non-object entries (e.g. nested arrays from bad server config)
          continue
        }

        if (!entry.has("urls")) continue

        try {
          // Handle "urls" which can be a String or an Array of Strings
          val urls = mutableListOf<String>()
          val urlsObj = entry.get("urls")
          if (urlsObj is JSONArray) {
            for (j in 0 until urlsObj.length()) {
              urls.add(urlsObj.getString(j))
            }
          } else {
            urls.add(urlsObj.toString())
          }

          val builder = PeerConnection.IceServer.builder(urls)
          if (entry.has("username")) {
            builder.setUsername(entry.getString("username"))
          }
          if (entry.has("credential")) {
            builder.setPassword(entry.getString("credential"))
          }
          servers.add(builder.createIceServer())
        } catch (e: Exception) {
          Log.w(TAG, "Skipping invalid ICE server entry at index $i", e)
        }
      }
      currentIceServers = servers
      Log.i(TAG, "Updated ICE servers configuration with ${servers.size} servers.")
    } catch (e: Exception) {
      Log.e(TAG, "Failed to parse ICE configuration: $jsonConfig", e)
      onError?.invoke("Connection Configuration Error: Failed to parse server settings.")
    }
  }

  @Suppress("unused")
  fun createPeerConnection(peerId: String, context: String) {
    val factory = peerConnectionFactory ?: run {
      Log.e(TAG, "Factory null in createPeerConnection")
      return
    }

    // Cleanup existing session if any
    sessions[peerId]?.remove(context)?.dispose()

    // Immediately report 'New' state to avoid "Disconnected" flicker in UI.
    // PeerConnectionState.New (0) maps to "Connecting" in the UI layer.
    if (context == "data") {
      nativeOnPeerConnectionStateChange(nativePointer, peerId, PeerConnectionState.New.value)
    }

    // Configuration for Vanilla ICE (Bundle candidates in SDP)
    // We use GATHER_ONCE to ensure we get a complete set before sending.
    val rtcConfig = RTCConfiguration(currentIceServers).apply {
      sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
      iceTransportsType = PeerConnection.IceTransportsType.ALL
      bundlePolicy = PeerConnection.BundlePolicy.MAXBUNDLE
      rtcpMuxPolicy = PeerConnection.RtcpMuxPolicy.REQUIRE
      continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_ONCE
    }

    val observer = object : PeerConnection.Observer {
      override fun onSignalingChange(state: PeerConnection.SignalingState?) {
        Log.d(TAG, "onSignalingChange ($peerId:$context): $state")
      }

      override fun onConnectionChange(newState: PeerConnection.PeerConnectionState?) {
        Log.i(TAG, "onConnectionChange ($peerId:$context): $newState")
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

      override fun onIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onStandardizedIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onIceConnectionReceivingChange(receiving: Boolean) {}

      override fun onIceGatheringChange(state: PeerConnection.IceGatheringState?) {
        Log.i(TAG, "onIceGatheringChange ($peerId:$context): $state")
        if (state == PeerConnection.IceGatheringState.COMPLETE) {
          // Signal the coroutine waiting in createOffer/createAnswer
          val session = getSession(peerId, context)
          session?.iceGatheringComplete?.complete(Unit)
        }
      }

      override fun onIceCandidate(candidate: IceCandidate?) {
        // Vanilla ICE: We do NOT send candidates individually.
        // We wait for gathering to complete and send the full SDP.
      }

      override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}
      override fun onAddStream(stream: MediaStream?) {}
      override fun onRemoveStream(stream: MediaStream?) {}
      override fun onRenegotiationNeeded() {}

      override fun onDataChannel(dc: DataChannel?) {
        dc ?: return
        Log.i(TAG, "onDataChannel received ($peerId)")
        val session = getSession(peerId, context) ?: return
        setupDataChannel(peerId, dc, session)
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

    sessions.getOrPut(peerId) { ConcurrentHashMap() }[context] = session
  }

  @Suppress("unused")
  fun createOffer(peerId: String, context: String) {
    val session = getSession(peerId, context) ?: return

    scope.launch {
      Log.i(TAG, "Creating OFFER for $peerId ($context)")

      // Reset gathering signal for new negotiation
      session.iceGatheringComplete = CompletableDeferred()

      val constraints = MediaConstraints()
      val offerSdp = suspendCancellableCoroutine<SessionDescription?> { cont ->
        session.pc.createOffer(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) {
            cont.resume(sdp)
          }

          override fun onSetSuccess() {}
          override fun onCreateFailure(s: String?) {
            Log.e(TAG, "createOffer failed: $s")
            cont.resume(null)
          }

          override fun onSetFailure(s: String?) {}
        }, constraints)
      } ?: return@launch

      // Set Local Description to trigger ICE Gathering
      val setSuccess = suspendCancellableCoroutine<Boolean> { cont ->
        session.pc.setLocalDescription(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) {}
          override fun onSetSuccess() {
            cont.resume(true)
          }

          override fun onCreateFailure(s: String?) {
            Log.e(TAG, "setLocalDescription (offer) failed: $s")
            cont.resume(false)
          }

          override fun onSetFailure(s: String?) {
            Log.e(TAG, "setLocalDescription (offer) setFail: $s")
            cont.resume(false)
          }
        }, offerSdp)
      }

      if (setSuccess) {
        Log.i(TAG, "Offer set as local description. Waiting for ICE gathering...")

        // Wait for gathering to complete (Vanilla ICE)
        // Timeout ensures we don't hang forever if network is flaky
        withTimeoutOrNull(GATHERING_TIMEOUT_MS) {
          session.iceGatheringComplete?.await()
        }

        // Get the updated SDP which now contains the candidates
        val finalSdp = session.pc.localDescription ?: offerSdp

        Log.i(TAG, "Sending OFFER with bundled candidates for $peerId")
        val message = JSONObject().apply {
          put("type", "offer")
          put("sdp", finalSdp.description)
          put("to", peerId)
          put("context", context)
        }
        sendSignalingMessage(message.toString())
      }
    }
  }

  @Suppress("unused")
  fun createAnswer(peerId: String, context: String) {
    val session = getSession(peerId, context) ?: return

    scope.launch {
      Log.i(TAG, "Creating ANSWER for $peerId ($context)")

      session.iceGatheringComplete = CompletableDeferred()

      if (context == "media") {
        ensureLocalMediaAttached(session)
      }

      val constraints = MediaConstraints().apply {
        mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveAudio", "true"))
        mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveVideo", "true"))
      }

      val answerSdp = suspendCancellableCoroutine<SessionDescription?> { cont ->
        session.pc.createAnswer(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) {
            cont.resume(sdp)
          }

          override fun onSetSuccess() {}
          override fun onCreateFailure(s: String?) {
            Log.e(TAG, "createAnswer failed: $s")
            cont.resume(null)
          }

          override fun onSetFailure(s: String?) {}
        }, constraints)
      } ?: return@launch

      val setSuccess = suspendCancellableCoroutine<Boolean> { cont ->
        session.pc.setLocalDescription(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) {}
          override fun onSetSuccess() {
            cont.resume(true)
          }

          override fun onCreateFailure(s: String?) {
            Log.e(TAG, "setLocalDescription (answer) failed: $s")
            cont.resume(false)
          }

          override fun onSetFailure(s: String?) {
            Log.e(TAG, "setLocalDescription (answer) setFail: $s")
            cont.resume(false)
          }
        }, answerSdp)
      }

      if (setSuccess) {
        Log.i(TAG, "Answer set as local description. Waiting for ICE gathering...")

        withTimeoutOrNull(GATHERING_TIMEOUT_MS) {
          session.iceGatheringComplete?.await()
        }

        val finalSdp = session.pc.localDescription ?: answerSdp

        Log.i(TAG, "Sending ANSWER with bundled candidates for $peerId")
        val message = JSONObject().apply {
          put("type", "answer")
          put("sdp", finalSdp.description)
          put("to", peerId)
          put("context", context)
        }
        sendSignalingMessage(message.toString())
      }
    }
  }

  fun setRemoteDescription(peerId: String, context: String, sdp: String, type: String) {
    val session = getSession(peerId, context) ?: run {
      Log.w(TAG, "Session not found for setRemoteDescription: $peerId ($context)")
      return
    }

    if (session.isDisposed) {
      Log.w(TAG, "Session disposed, ignoring setRemoteDescription for $peerId")
      return
    }

    Log.d(TAG, "Setting Remote Description ($type) for $peerId")

    val canonicalType = when (type.lowercase()) {
      "offer" -> SessionDescription.Type.OFFER
      "answer" -> SessionDescription.Type.ANSWER
      "pranswer" -> SessionDescription.Type.PRANSWER
      else -> {
        Log.e(TAG, "Invalid SDP type: $type")
        return
      }
    }

    // Ensure valid SDP string
    if (sdp.isBlank()) {
      Log.e(TAG, "Remote SDP is empty for $peerId")
      return
    }

    val sdpObj = SessionDescription(canonicalType, sdp)

    session.pc.setRemoteDescription(object : SdpObserver {
      override fun onCreateSuccess(sdp: SessionDescription?) {}
      override fun onSetSuccess() {
        Log.i(TAG, "Remote description ($type) set successfully for $peerId")
      }

      override fun onCreateFailure(s: String?) {
        Log.e(TAG, "Failed to create remote desc (unexpected): $s")
      }

      override fun onSetFailure(s: String?) {
        Log.e(TAG, "Failed to set remote description for peer $peerId: $s")
      }
    }, sdpObj)
  }

  @Suppress("unused")
  fun createDataChannel(peerId: String) {
    val session = getSession(peerId, "data") ?: return
    Log.i(TAG, "Creating DataChannel for $peerId")

    val init = DataChannel.Init()
    val dc = session.pc.createDataChannel("data", init)
    setupDataChannel(peerId, dc, session)
  }

  private fun setupDataChannel(peerId: String, dc: DataChannel, session: ConnectionSession) {
    session.dataChannel = dc
    dc.registerObserver(object : DataChannel.Observer {
      override fun onBufferedAmountChange(amount: Long) {}
      override fun onStateChange() {
        Log.i(TAG, "DataChannel state changed: ${dc.state()}")
        if (dc.state() == DataChannel.State.OPEN) {
          nativeOnDataChannelOpen(nativePointer, peerId)
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

    // Check immediate open state
    if (dc.state() == DataChannel.State.OPEN) {
      nativeOnDataChannelOpen(nativePointer, peerId)
    }
  }

  // --- Helpers ---

  private fun getSession(peerId: String, context: String): ConnectionSession? {
    return sessions[peerId]?.get(context)
  }

  private fun ensureLocalMediaAttached(session: ConnectionSession) {
    if (localVideoTrack == null || localAudioTrack == null) startLocalMedia()

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

  // --- Media Management ---

  fun startLocalMedia() {
    if (peerConnectionFactory == null) initializeFactory()
    if (localVideoTrack != null || localAudioTrack != null) return

    try {
      val enumerator = Camera2Enumerator(context)
      val deviceName = enumerator.deviceNames.firstOrNull { enumerator.isFrontFacing(it) }
        ?: enumerator.deviceNames.firstOrNull()

      if (deviceName != null) {
        videoCapturer = enumerator.createCapturer(deviceName, null)
        val factory = peerConnectionFactory!!
        val source = factory.createVideoSource(videoCapturer!!.isScreencast)
        videoSource = source

        val surfaceHelper = SurfaceTextureHelper.create("CaptureThread", eglBase.eglBaseContext)
        videoCapturer!!.initialize(surfaceHelper, context, source.capturerObserver)
        videoCapturer!!.startCapture(640, 480, 30) // Standard resolution

        localVideoTrack = factory.createVideoTrack(VIDEO_TRACK_ID, source)
        localSink?.let { localVideoTrack?.addSink(it) }
      }

      val audioConstraints = MediaConstraints()
      audioSource = peerConnectionFactory!!.createAudioSource(audioConstraints)
      localAudioTrack = peerConnectionFactory!!.createAudioTrack(AUDIO_TRACK_ID, audioSource)

    } catch (e: Exception) {
      Log.e(TAG, "Failed to start local media", e)
    }
  }

  fun stopLocalMedia() {
    try {
      localVideoTrack?.let {
        localSink?.let { sink -> it.removeSink(sink) }
        it.dispose()
      }
      localVideoTrack = null

      videoCapturer?.stopCapture()
      videoCapturer?.dispose()
      videoCapturer = null

      videoSource?.dispose()
      videoSource = null

      localAudioTrack?.dispose()
      localAudioTrack = null

      audioSource?.dispose()
      audioSource = null
    } catch (e: Exception) {
      Log.e(TAG, "Error stopping media", e)
    }
  }

  fun addVideoTrack(peerId: String) {
    getSession(peerId, "media")?.let { ensureLocalMediaAttached(it) }
  }

  fun addAudioTrack(peerId: String) {
    getSession(peerId, "media")?.let { ensureLocalMediaAttached(it) }
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

  fun closePeerConnection(peerId: String, context: String) {
    sessions[peerId]?.remove(context)?.dispose()
    if (context == "media") {
      remoteVideoTracks.remove(peerId)
      remoteSinks.remove(peerId)
    }
  }

  fun removePeer(peerId: String) {
    sessions.remove(peerId)?.values?.forEach { it.dispose() }
  }

  // --- Signaling ---

  fun sendSignalingMessage(message: String) {
    signalingClient.send(JSONObject(message))
  }

  @Suppress("unused")
  fun sendDataChannelMessage(peerId: String, message: String): Boolean {
    val dc = getSession(peerId, "data")?.dataChannel ?: return false
    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(message.toByteArray())
      return dc.send(DataChannel.Buffer(buffer, false))
    }
    return false
  }

  @Suppress("unused")
  fun sendDataChannelMessageBinary(peerId: String, data: ByteArray): Boolean {
    val dc = getSession(peerId, "data")?.dataChannel ?: return false
    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(data)
      return dc.send(DataChannel.Buffer(buffer, true))
    }
    return false
  }

  @Suppress("unused")
  fun getPeerConnectionState(peerId: String): Int {
    val state = getSession(peerId, "data")?.pc?.connectionState()
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
          is SignalingMessage.SystemMessage -> nativeOnSystemMessage(
            nativePointer,
            message.type,
            message.payload.toString()
          )
        }
      }
    }.launchIn(scope)
  }

  private fun observeSignalingState() {
    if (isObservingSignalingState) return
    isObservingSignalingState = true
    signalingClient.state.onEach { state ->
      scope.launch {
        nativeOnSignalingConnectionStateChange(nativePointer, state.ordinal)
      }
    }.launchIn(scope)
  }

  companion object {
    private const val TAG = "WebRtcTransport"
    private const val MEDIA_STREAM_ID = "ARDAMS"
    private const val VIDEO_TRACK_ID = "ARDAMSv0"
    private const val AUDIO_TRACK_ID = "ARDAMSa0"
    private const val GATHERING_TIMEOUT_MS = 1500L // Reduced wait for candidates

    val eglBase: EglBase by lazy { EglBase.create() }
  }
}
