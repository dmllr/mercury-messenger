package hela.eillance.webrtc

import android.content.Context
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
import org.webrtc.DataChannel
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
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.coroutines.resume

class WebRtcDataTransport(private val context: Context, private val nativePointer: Long) {

  private class ConnectionSession(val pc: PeerConnection) {
    var dataChannel: DataChannel? = null
    var iceGatheringComplete: CompletableDeferred<Unit>? = null
    @Volatile
    var isDisposed = false
    val dataChannelOpenReported = AtomicBoolean(false)

    fun dispose() {
      if (isDisposed) return
      isDisposed = true
      try {
        dataChannel?.unregisterObserver(); dataChannel?.dispose()
      } catch (_: Exception) {
      }
      try {
        pc.close()
      } catch (_: Exception) {
      }
    }
  }

  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private var signalingClient = SignalingClient()
  private var peerConnectionFactory: PeerConnectionFactory? = null
  private val sessions = ConcurrentHashMap<String, ConnectionSession>()

  private var localPeerId: String? = null
  private var currentSignalingUrl: String = ""
  private var isObservingMessages = false
  private var isObservingSignalingState = false

  private var currentIceServers: List<PeerConnection.IceServer> = listOf(
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

  private fun initializeFactory() {
    if (peerConnectionFactory != null) return
    PeerConnectionFactory.initialize(
      PeerConnectionFactory.InitializationOptions.builder(context).setEnableInternalTracer(false)
        .createInitializationOptions()
    )
    peerConnectionFactory = PeerConnectionFactory.builder().createPeerConnectionFactory()
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
    sessions.values.forEach { it.dispose() }
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
      onError?.invoke("Config Error: Failed to parse ICE servers.")
    }
  }

  @Suppress("unused")
  fun connectPeer(peerId: String) {
    if (sessions.containsKey(peerId)) return
    val factory = peerConnectionFactory ?: return
    nativeOnPeerConnectionStateChange(nativePointer, peerId, 0, PeerConnectionState.New.value) // 0=Data

    val rtcConfig = RTCConfiguration(currentIceServers).apply {
      sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
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
        nativeOnPeerConnectionStateChange(nativePointer, peerId, 0, stateInt)
      }

      override fun onIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onStandardizedIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onIceConnectionReceivingChange(receiving: Boolean) {}
      override fun onIceGatheringChange(state: PeerConnection.IceGatheringState?) {
        if (state == PeerConnection.IceGatheringState.COMPLETE) sessions[peerId]?.iceGatheringComplete?.complete(Unit)
      }

      override fun onIceCandidate(candidate: IceCandidate?) {}
      override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}
      override fun onAddStream(stream: MediaStream?) {}
      override fun onRemoveStream(stream: MediaStream?) {}
      override fun onRenegotiationNeeded() {}
      override fun onDataChannel(dc: DataChannel?) {
        dc ?: return
        val session = sessions[peerId] ?: return
        setupDataChannel(peerId, dc, session)
      }

      override fun onTrack(transceiver: RtpTransceiver?) {}
      override fun onAddTrack(receiver: RtpReceiver, streams: Array<out MediaStream>) {}
    }

    val pc = factory.createPeerConnection(rtcConfig, observer) ?: return
    sessions[peerId] = ConnectionSession(pc)
  }

  @Suppress("unused")
  fun createOffer(peerId: String) {
    val session = sessions[peerId] ?: return
    val init = DataChannel.Init()
    val dc = session.pc.createDataChannel("data", init)
    setupDataChannel(peerId, dc, session)

    scope.launch {
      session.iceGatheringComplete = CompletableDeferred()
      val offerSdp = suspendCancellableCoroutine<SessionDescription?> { cont ->
        session.pc.createOffer(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) = cont.resume(sdp)
          override fun onSetSuccess() {}
          override fun onCreateFailure(s: String?) = cont.resume(null)
          override fun onSetFailure(s: String?) {}
        }, MediaConstraints())
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
        withTimeoutOrNull(1500L) { session.iceGatheringComplete?.await() }
        val finalSdp = session.pc.localDescription ?: offerSdp
        val message = JSONObject().apply {
          put("type", "offer")
          put("sdp", finalSdp.description)
          put("context", "data")
          put("to", peerId)
        }
        sendSignalingMessage(message.toString())
      }
    }
  }

  fun createAnswer(peerId: String) {
    val session = sessions[peerId] ?: return
    scope.launch {
      session.iceGatheringComplete = CompletableDeferred()
      val answerSdp = suspendCancellableCoroutine<SessionDescription?> { cont ->
        session.pc.createAnswer(object : SdpObserver {
          override fun onCreateSuccess(sdp: SessionDescription?) = cont.resume(sdp)
          override fun onSetSuccess() {}
          override fun onCreateFailure(s: String?) = cont.resume(null)
          override fun onSetFailure(s: String?) {}
        }, MediaConstraints())
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
        withTimeoutOrNull(1500L) { session.iceGatheringComplete?.await() }
        val finalSdp = session.pc.localDescription ?: answerSdp
        val message = JSONObject().apply {
          put("type", "answer")
          put("sdp", finalSdp.description)
          put("context", "data")
          put("to", peerId)
        }
        sendSignalingMessage(message.toString())
      }
    }
  }

  fun setRemoteDescription(peerId: String, sdp: String, type: String) {
    val session = sessions[peerId] ?: return
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

  private fun setupDataChannel(peerId: String, dc: DataChannel, session: ConnectionSession) {
    session.dataChannel = dc
    dc.registerObserver(object : DataChannel.Observer {
      override fun onBufferedAmountChange(amount: Long) {}
      override fun onStateChange() {
        if (dc.state() == DataChannel.State.OPEN && session.dataChannelOpenReported.compareAndSet(false, true)) {
          nativeOnDataChannelOpen(nativePointer, peerId)
        }
      }

      override fun onMessage(buffer: DataChannel.Buffer) {
        val data = ByteArray(buffer.data.remaining())
        buffer.data.get(data)
        if (buffer.binary) nativeOnDataChannelMessageBinary(nativePointer, peerId, data)
        else nativeOnDataChannelMessage(nativePointer, peerId, String(data))
      }
    })
    if (dc.state() == DataChannel.State.OPEN && session.dataChannelOpenReported.compareAndSet(false, true)) {
      nativeOnDataChannelOpen(nativePointer, peerId)
    }
  }

  @Suppress("unused")
  fun disconnectPeer(peerId: String) {
    val session = sessions.remove(peerId)
    if (session != null) {
      scope.launch { session.dispose() }
    }
  }

  fun sendSignalingMessage(message: String) {
    signalingClient.send(JSONObject(message))
  }

  fun sendDataChannelMessage(peerId: String, message: String): Boolean {
    val session = sessions[peerId] ?: return false
    val dc = session.dataChannel ?: return false
    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(message.toByteArray())
      return dc.send(DataChannel.Buffer(buffer, false))
    }
    return false
  }

  @Suppress("unused")
  fun sendDataChannelMessageBinary(peerId: String, data: ByteArray): Boolean {
    val session = sessions[peerId] ?: return false
    val dc = session.dataChannel ?: return false
    if (dc.state() == DataChannel.State.OPEN) {
      val buffer = ByteBuffer.wrap(data)
      return dc.send(DataChannel.Buffer(buffer, true))
    }
    return false
  }

  fun isDataChannelOpen(peerId: String): Boolean {
    val session = sessions[peerId] ?: return false
    val dc = session.dataChannel ?: return false
    return dc.state() == DataChannel.State.OPEN
  }

  @Suppress("unused")
  fun getPeerConnectionState(peerId: String): Int {
    val state = sessions[peerId]?.pc?.connectionState()
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
      scope.launch { nativeOnSignalingConnectionStateChange(nativePointer, state.ordinal) }
    }.launchIn(scope)
  }
}
