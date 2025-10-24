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
import org.json.JSONArray
import org.json.JSONObject
import org.webrtc.AudioTrack
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
import org.webrtc.SessionDescription
import org.webrtc.VideoTrack
import java.util.concurrent.ConcurrentHashMap

class WebRtcTransport(context: Context, private val nativePointer: Long = 0L) {
    private val coroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val signalingClient = SignalingClient()
    private val eglBase: EglBase = EglBase.create()

    private val peerConnectionFactory: PeerConnectionFactory
    private val peers = ConcurrentHashMap<String, PeerConnection>()
    private var isObservingMessages = false

    private external fun nativeOnPeerMessage(nativePointer: Long, message: String)
    private external fun nativeOnSystemMessage(nativePointer: Long, type: String, message: String)
    private external fun nativeOnPeerOnline(nativePointer: Long, peerId: String)
    private external fun nativeOnPeerOffline(nativePointer: Long, peerId: String)
    private external fun nativeOnSignalingError(nativePointer: Long, peerId: String, error: String)

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

    fun connect() {
        observeSignaling()
        signalingClient.connect()
    }

    fun terminate() {
        signalingClient.disconnect()
        peers.values.forEach { it.close() }
        peers.clear()
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
            override fun onSignalingChange(p0: PeerConnection.SignalingState?) {}
            override fun onIceConnectionChange(p0: PeerConnection.IceConnectionState?) {}
            override fun onIceConnectionReceivingChange(p0: Boolean) {}
            override fun onIceGatheringChange(p0: PeerConnection.IceGatheringState?) {}
            override fun onIceCandidate(candidate: IceCandidate) {
                val candidateJson = JSONObject().apply {
                    put("type", "candidate")
                    put("to", peerId)
                    put("context", context)
                    put("sdp", candidate.sdp)
                    put("sdpMLineIndex", candidate.sdpMLineIndex)
                    put("sdpMid", candidate.sdpMid)
                }
                sendSignalingMessage(candidateJson.toString())
            }

            override fun onIceCandidatesRemoved(p0: Array<out IceCandidate>?) {}
            override fun onAddStream(stream: MediaStream?) {
                stream?.videoTracks?.firstOrNull()?.let {
                    // onTrack("video", it)
                }
                stream?.audioTracks?.firstOrNull()?.let {
                    // onTrack("audio", it)
                }
            }

            override fun onRemoveStream(p0: MediaStream?) {}
            override fun onDataChannel(p0: org.webrtc.DataChannel?) {}
            override fun onRenegotiationNeeded() {}
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
