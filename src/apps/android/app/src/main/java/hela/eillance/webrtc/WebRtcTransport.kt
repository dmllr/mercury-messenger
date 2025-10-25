package hela.eillance.webrtc

import android.content.Context
import android.util.Log
import hela.eillance.ndkio.PeerConnectionState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import org.json.JSONObject
import org.webrtc.AudioTrack
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
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import org.webrtc.VideoTrack
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap

class WebRtcTransport(context: Context, private val nativePointer: Long) {
    private val coroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val signalingClient = SignalingClient()
    private val eglBase: EglBase = EglBase.create()

    private val peerConnectionFactory: PeerConnectionFactory
    private val peers = ConcurrentHashMap<String, PeerConnection>()
    private val dataChannels = ConcurrentHashMap<String, DataChannel>()
    private var isObservingMessages = false

    private external fun nativeOnPeerMessage(nativePointer: Long, message: String)
    private external fun nativeOnSystemMessage(nativePointer: Long, type: String, message: String)
    private external fun nativeOnPeerOnline(nativePointer: Long, peerId: String)
    private external fun nativeOnPeerOffline(nativePointer: Long, peerId: String)
    private external fun nativeOnSignalingError(nativePointer: Long, peerId: String, error: String)
    private external fun nativeOnDataChannelMessage(nativePointer: Long, peerId: String, message: String)
    private external fun nativeOnDataChannelOpen(nativePointer: Long, peerId: String)


    init {
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

    fun connect(peerId: String) {
        observeSignaling()
        signalingClient.connect(peerId)
    }

    fun terminate() {
        signalingClient.disconnect()
        peers.values.forEach { it.close() }
        peers.clear()
        dataChannels.clear()
        peerConnectionFactory.dispose()
        eglBase.release()
        coroutineScope.cancel()
    }

    fun createPeerConnection(peerId: String, context: String) {
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
            iceTransportsType = PeerConnection.IceTransportsType.ALL
        }

        val observer = object : PeerConnection.Observer {
            override fun onSignalingChange(newState: PeerConnection.SignalingState?) {
                Log.i(TAG, "onSignalingChange for peer $peerId ($context): $newState")
            }
            override fun onIceConnectionChange(newState: PeerConnection.IceConnectionState?) {
                Log.i(TAG, "onIceConnectionChange for peer $peerId ($context): $newState")
            }
            override fun onIceConnectionReceivingChange(p0: Boolean) {}
            override fun onIceGatheringChange(newState: PeerConnection.IceGatheringState?) {
                Log.i(TAG, "onIceGatheringChange for peer $peerId ($context): $newState")
            }
            override fun onIceCandidate(candidate: IceCandidate?) {
                if (candidate == null) return
                val candidateJson = JSONObject().apply {
                    put("type", "candidate")
                    put("to", peerId)
                    put("context", context)
                    put("candidate", candidate.sdp)
                    put("sdpMLineIndex", candidate.sdpMLineIndex)
                    put("sdpMid", candidate.sdpMid)
                }
                sendSignalingMessage(candidateJson.toString())
            }

            override fun onIceCandidatesRemoved(p0: Array<out IceCandidate>?) {}
            override fun onAddStream(stream: MediaStream?) {
                Log.i(TAG, "onAddStream for peer $peerId ($context)")
                stream?.videoTracks?.firstOrNull()?.let {
// onTrack("video", it)
                }
                stream?.audioTracks?.firstOrNull()?.let {
// onTrack("audio", it)
                }
            }

            override fun onRemoveStream(p0: MediaStream?) {}
            override fun onDataChannel(dc: DataChannel?) {
                dc ?: return
                Log.d(TAG, "onDataChannel received for label: ${dc.label()} from $peerId")
                dataChannels[peerId] = dc
                val dataChannelObserver = object : DataChannel.Observer {
                    override fun onBufferedAmountChange(p0: Long) {}
                    override fun onStateChange() {
                        Log.d(TAG, "Incoming data channel state for $peerId changed to: ${dc.state()}")
                        if (dc.state() == DataChannel.State.OPEN) {
                            nativeOnDataChannelOpen(nativePointer, peerId)
                        }
                    }
                    override fun onMessage(buffer: DataChannel.Buffer?) {
                        buffer ?: return
                        val data = ByteArray(buffer.data.remaining())
                        buffer.data.get(data)
                        nativeOnDataChannelMessage(nativePointer, peerId, String(data))
                    }
                }
                dc.registerObserver(dataChannelObserver)
            }
            override fun onRenegotiationNeeded() {
                Log.i(TAG, "onRenegotiationNeeded for peer: $peerId, context: $context")
                peers[peerId]?.let { pc ->
                    if (pc.signalingState() == PeerConnection.SignalingState.STABLE) {
                        Log.i(TAG, "Signaling state is stable, creating offer for $peerId")
                        createOffer(peerId, context)
                    } else {
                        Log.w(TAG, "Renegotiation needed for $peerId but signaling state is ${pc.signalingState()}, suppressing offer creation.")
                    }
                }
            }
            override fun onAddTrack(rtpReceiver: RtpReceiver, mediaStreams: Array<out MediaStream>) {
                rtpReceiver.track()?.let { track ->
                    when (track) {
                        is VideoTrack -> Log.d(TAG, "Video track received") // onTrack("video", track)
                        is AudioTrack -> Log.d(TAG, "Audio track received") // onTrack("audio", track)
                    }
                }
            }
        }
        peers[peerId] = peerConnectionFactory.createPeerConnection(rtcConfig, observer) ?: return
    }

    fun createDataChannel(peerId: String) {
        val pc = peers[peerId] ?: return
        Log.d(TAG, "Creating data channel for peer: $peerId")
        val dc = pc.createDataChannel("data", DataChannel.Init())
        dataChannels[peerId] = dc
        val dataChannelObserver = object : DataChannel.Observer {
            override fun onBufferedAmountChange(p0: Long) {}
            override fun onStateChange() {
                Log.d(TAG, "Outgoing data channel state for $peerId changed to: ${dc.state()}")
                if (dc.state() == DataChannel.State.OPEN) {
                    nativeOnDataChannelOpen(nativePointer, peerId)
                }
            }
            override fun onMessage(buffer: DataChannel.Buffer?) {
                buffer ?: return
                val data = ByteArray(buffer.data.remaining())
                buffer.data.get(data)
                nativeOnDataChannelMessage(nativePointer, peerId, String(data))
            }
        }
        dc.registerObserver(dataChannelObserver)
    }

    fun sendDataChannelMessage(peerId: String, message: String) {
        val dc = dataChannels[peerId]
        if (dc == null) {
            Log.e(TAG, "DataChannel for peer $peerId not found.")
            return
        }
        if (dc.state() == DataChannel.State.OPEN) {
            val buffer = ByteBuffer.wrap(message.toByteArray())
            dc.send(DataChannel.Buffer(buffer, false))
        } else {
            Log.w(TAG, "Attempted to send message on non-open DataChannel for peer $peerId. State: ${dc.state()}")
        }
    }

    fun createOffer(peerId: String, context: String) {
        val pc = peers[peerId] ?: return
        val sdpObserver = object : SdpObserver {
            override fun onCreateSuccess(sdp: SessionDescription?) { if (sdp != null) pc.setLocalDescription(this, sdp) }
            override fun onSetSuccess() {
                pc.localDescription?.let { localSdp ->
                    val message = JSONObject().apply {
                        put("type", localSdp.type.canonicalForm())
                        put("sdp", localSdp.description)
                        put("to", peerId)
                        put("context", context)
                    }
                    sendSignalingMessage(message.toString())
                }
            }
            override fun onCreateFailure(s: String?) { Log.e(TAG, "createOffer failed: $s") }
            override fun onSetFailure(s: String?) { Log.e(TAG, "setLocalDescription failed: $s") }
        }
        pc.createOffer(sdpObserver, MediaConstraints())
    }

    fun createAnswer(peerId: String, context: String) {
        val pc = peers[peerId] ?: return
        val sdpObserver = object : SdpObserver {
            override fun onCreateSuccess(sdp: SessionDescription?) { if (sdp != null) pc.setLocalDescription(this, sdp) }
            override fun onSetSuccess() {
                pc.localDescription?.let { localSdp ->
                    val message = JSONObject().apply {
                        put("type", localSdp.type.canonicalForm())
                        put("sdp", localSdp.description)
                        put("to", peerId)
                        put("context", context)
                    }
                    sendSignalingMessage(message.toString())
                }
            }
            override fun onCreateFailure(s: String?) { Log.e(TAG, "createAnswer failed: $s") }
            override fun onSetFailure(s: String?) { Log.e(TAG, "setLocalDescription failed: $s") }
        }
        pc.createAnswer(sdpObserver, MediaConstraints())
    }

    fun setRemoteDescription(peerId: String, context: String, sdp: String, type: String) {
        val pc = peers[peerId] ?: return
        val sdpObserver = object : SdpObserver {
            override fun onCreateSuccess(sdp: SessionDescription?) {}
            override fun onSetSuccess() {}
            override fun onCreateFailure(s: String?) {}
            override fun onSetFailure(s: String?) { Log.e(TAG, "Failed to set remote description: $s") }
        }
        val sdpType = SessionDescription.Type.fromCanonicalForm(type)
        pc.setRemoteDescription(sdpObserver, SessionDescription(sdpType, sdp))
    }

    fun addRemoteCandidate(peerId: String, context: String, candidateSdp: String, mid: String) {
        val pc = peers[peerId] ?: return
        val candidate = IceCandidate(mid, -1, candidateSdp)
        pc.addIceCandidate(candidate)
    }

    fun sendSignalingMessage(message: String) {
        signalingClient.send(JSONObject(message))
    }

    fun getPeerConnectionState(peerId: String): Int {
        val state = peers[peerId]?.connectionState()
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

    private fun observeSignaling() {
        if (isObservingMessages) return
        isObservingMessages = true

        signalingClient.messages.onEach { message ->
            coroutineScope.launch {
                when (message) {
                    is SignalingMessage.PeerMessage -> nativeOnPeerMessage(nativePointer, message.message.toString())
                    is SignalingMessage.Error -> nativeOnSignalingError(nativePointer, message.forPeer ?: "", message.error)
                    is SignalingMessage.PeerOnline -> nativeOnPeerOnline(nativePointer, message.peerId)
                    is SignalingMessage.PeerOffline -> nativeOnPeerOffline(nativePointer, message.peerId)
                    is SignalingMessage.SystemMessage -> nativeOnSystemMessage(nativePointer, message.type, message.payload.toString())
                }
            }
        }.launchIn(coroutineScope)
    }

    companion object {
        private const val TAG = "WebRtcTransport"
    }
}
