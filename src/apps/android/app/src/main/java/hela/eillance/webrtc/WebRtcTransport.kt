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
import org.webrtc.AudioSource
import org.webrtc.AudioTrack
import org.webrtc.Camera2Enumerator
import org.webrtc.CameraVideoCapturer
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
import org.webrtc.SurfaceTextureHelper
import org.webrtc.VideoCapturer
import org.webrtc.VideoSink
import org.webrtc.VideoSource
import org.webrtc.VideoTrack
import java.nio.ByteBuffer
import java.util.concurrent.ConcurrentHashMap

class WebRtcTransport(private val context: Context, private val nativePointer: Long) {
    private val coroutineScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val signalingClient = SignalingClient()
    private val eglBase: EglBase = WebRtcTransport.eglBase

    private val peerConnectionFactory: PeerConnectionFactory
    private val peers = ConcurrentHashMap<String, PeerConnection>()
    private val dataChannels = ConcurrentHashMap<String, DataChannel>()

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

    private var isObservingMessages = false

    private external fun nativeOnPeerMessage(nativePointer: Long, message: String)
    private external fun nativeOnSystemMessage(nativePointer: Long, type: String, message: String)
    private external fun nativeOnPeerOnline(nativePointer: Long, peerId: String)
    private external fun nativeOnPeerOffline(nativePointer: Long, peerId: String)
    private external fun nativeOnSignalingError(nativePointer: Long, peerId: String, error: String)
    private external fun nativeOnDataChannelMessage(nativePointer: Long, peerId: String, message: String)
    private external fun nativeOnDataChannelOpen(nativePointer: Long, peerId: String)


    init {
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

    fun connect(peerId: String) {
        observeSignaling()
        signalingClient.connect(peerId)
    }

    fun terminate() {
        signalingClient.disconnect()
        stopLocalMedia()
        peers.values.forEach { it.close() }
        peers.clear()
        dataChannels.clear()
        peerConnectionFactory.dispose()
        // Do not release the singleton EGL base here, it's application-scoped.
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
            sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
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
                if (candidate == null) {
                    Log.d(TAG, "End of ICE candidates for peer $peerId ($context)")
                    return
                }
                val candidateJson = JSONObject().apply {
                    put("type", "candidate")
                    put("to", peerId)
                    put("context", context)
                    put("candidate", candidate.sdp)
                    put("sdpMLineIndex", candidate.sdpMLineIndex)
                    put("sdpMid", candidate.sdpMid)
                }
                sendSignalingMessage(candidateJson.toString())
                Log.d(TAG, "onIceCandidate for peer $peerId ($context): ${candidate.sdp}")
            }

            override fun onIceCandidatesRemoved(p0: Array<out IceCandidate>?) {}

            override fun onAddStream(stream: MediaStream?) {
                Log.w(TAG, "onAddStream called (deprecated) for peer $peerId ($context). Ignoring.")
            }

            override fun onRemoveStream(p0: MediaStream?) {}

            override fun onDataChannel(dc: DataChannel?) {
                dc ?: return
                Log.i(TAG, "onDataChannel received for label: ${dc.label()} from $peerId")
                dataChannels[peerId] = dc
                val dataChannelObserver = object : DataChannel.Observer {
                    override fun onBufferedAmountChange(p0: Long) {}
                    override fun onStateChange() {
                        Log.i(TAG, "Incoming data channel state for $peerId changed to: ${dc.state()}")
                        if (dc.state() == DataChannel.State.OPEN) {
                            nativeOnDataChannelOpen(nativePointer, peerId)
                        }
                    }
                    override fun onMessage(buffer: DataChannel.Buffer?) {
                        buffer ?: return
                        val data = ByteArray(buffer.data.remaining())
                        buffer.data.get(data)
                        Log.d(TAG, "Received data channel message from $peerId")
                        nativeOnDataChannelMessage(nativePointer, peerId, String(data))
                    }
                }
                dc.registerObserver(dataChannelObserver)
            }
            override fun onRenegotiationNeeded() {
                Log.i(TAG, "onRenegotiationNeeded for peer: $peerId, context: $context")
                // This event fires when a change requires negotiation, like adding a track.
                // The typical response is to create a new offer. We should only do this
                // if the signaling state is stable to avoid conflicting with an ongoing exchange.
                val pc = peers[peerId]
                if (pc?.signalingState() == PeerConnection.SignalingState.STABLE) {
                    Log.d(TAG, "Signaling state is STABLE, creating offer due to renegotiation.")
                    createOffer(peerId, context)
                } else {
                    Log.w(TAG, "onRenegotiationNeeded was called, but signaling state is ${pc?.signalingState()}. Suppressing offer creation to avoid glare.")
                }
            }
            override fun onAddTrack(rtpReceiver: RtpReceiver, mediaStreams: Array<out MediaStream>) {
                rtpReceiver.track()?.let { track ->
                    Log.i(TAG, "onAddTrack: Received ${track.kind()} track from $peerId")
                    when (track) {
                        is VideoTrack -> {
                            remoteVideoTracks[peerId] = track
                            // If a sink is already waiting for this track, attach it now.
                            remoteSinks[peerId]?.let { sink ->
                                Log.i(TAG, "Sink was ready, attaching remote video track for $peerId to sink.")
                                track.addSink(sink)
                            }
                        }
                        is AudioTrack -> {
                            Log.i(TAG, "Audio track received from $peerId via onAddTrack")
                            track.setEnabled(true)
                        }
                    }
                }
            }
        }
        Log.i(TAG, "Creating PeerConnection for peer $peerId, context: $context")
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
                Log.i(TAG, "Outgoing data channel state for $peerId changed to: ${dc.state()}")
                if (dc.state() == DataChannel.State.OPEN) {
                    nativeOnDataChannelOpen(nativePointer, peerId)
                }
            }
            override fun onMessage(buffer: DataChannel.Buffer?) {
                buffer ?: return
                val data = ByteArray(buffer.data.remaining())
                buffer.data.get(data)
                Log.d(TAG, "Received data channel message from $peerId (outgoing DC)")
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
            Log.d(TAG, "Sent data channel message to $peerId")
        } else {
            Log.w(TAG, "Attempted to send message on non-open DataChannel for peer $peerId. State: ${dc.state()}")
        }
    }

    fun createOffer(peerId: String, context: String) {
        val pc = peers[peerId] ?: return
        Log.d(TAG, "Creating offer for peer $peerId ($context)")
        val sdpObserver = object : SdpObserver {
            override fun onCreateSuccess(sdp: SessionDescription?) {
                Log.d(TAG, "Offer created for $peerId. SDP: ${sdp?.description?.take(200)}...")
                if (sdp != null) pc.setLocalDescription(this, sdp)
            }
            override fun onSetSuccess() {
                Log.i(TAG, "Local description (offer) set successfully for $peerId")
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
            override fun onCreateFailure(s: String?) { Log.e(TAG, "createOffer failed for peer $peerId: $s") }
            override fun onSetFailure(s: String?) { Log.e(TAG, "setLocalDescription failed for peer $peerId: $s") }
        }
        pc.createOffer(sdpObserver, MediaConstraints())
    }

    fun createAnswer(peerId: String, context: String) {
        val pc = peers[peerId] ?: return
        Log.d(TAG, "Creating answer for peer $peerId ($context)")

        // Fix for race condition: Ensure local tracks are represented by senders before creating an answer.
        // This is necessary because the JNI calls from C++ to add tracks are asynchronous.
        if (context == "media") {
            if (localAudioTrack != null && pc.senders.none { it.track()?.kind() == "audio" }) {
                Log.d(TAG, "Audio track not found in senders, adding it now before creating answer.")
                pc.addTrack(localAudioTrack, listOf(MEDIA_STREAM_ID))
            }
            if (localVideoTrack != null && pc.senders.none { it.track()?.kind() == "video" }) {
                Log.d(TAG, "Video track not found in senders, adding it now before creating answer.")
                pc.addTrack(localVideoTrack, listOf(MEDIA_STREAM_ID))
            }
        }

        val sdpObserver = object : SdpObserver {
            override fun onCreateSuccess(sdp: SessionDescription?) {
                Log.d(TAG, "Answer created for $peerId. SDP: ${sdp?.description?.take(200)}...")
                if (sdp != null) pc.setLocalDescription(this, sdp)
            }
            override fun onSetSuccess() {
                Log.i(TAG, "Local description (answer) set successfully for $peerId")
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
            override fun onCreateFailure(s: String?) { Log.e(TAG, "createAnswer failed for peer $peerId: $s") }
            override fun onSetFailure(s: String?) { Log.e(TAG, "setLocalDescription failed for peer $peerId: $s") }
        }
        val answerConstraints = MediaConstraints().apply {
            mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveAudio", "true"))
            mandatory.add(MediaConstraints.KeyValuePair("OfferToReceiveVideo", "true"))
        }
        pc.createAnswer(sdpObserver, answerConstraints)
    }

    fun setRemoteDescription(peerId: String, context: String, sdp: String, type: String) {
        Log.i(TAG, "Setting remote description for peer $peerId ($context), type: $type")
        val pc = peers[peerId] ?: run { Log.e(TAG, "PeerConnection for $peerId not found for setRemoteDescription"); return }
        val sdpObserver = object : SdpObserver {
            override fun onCreateSuccess(sdp: SessionDescription?) {}
            override fun onSetSuccess() {
                Log.i(TAG, "Set remote description successfully for peer $peerId.")
            }
            override fun onCreateFailure(s: String?) {}
            override fun onSetFailure(s: String?) { Log.e(TAG, "Failed to set remote description for peer $peerId: $s") }
        }
        val sdpType = SessionDescription.Type.fromCanonicalForm(type)
        pc.setRemoteDescription(sdpObserver, SessionDescription(sdpType, sdp))
    }

    fun addRemoteCandidate(peerId: String, context: String, candidateSdp: String, mid: String) {
        val candidate = IceCandidate(mid, -1, candidateSdp)
        Log.d(TAG, "Adding remote ICE candidate for peer $peerId ($context): $candidateSdp")
        val pc = peers[peerId]
        if (pc == null) { Log.e(TAG, "PeerConnection for $peerId not found, cannot add remote candidate."); return }
        pc.addIceCandidate(candidate)
    }

    fun addVideoTrack(peerId: String) {
        val pc = peers[peerId] ?: return
        if (localVideoTrack == null) startLocalMedia()
        localVideoTrack?.let {
            pc.addTrack(it, listOf(MEDIA_STREAM_ID))
            Log.d(TAG, "Local video track added to peer connection for $peerId")
        } ?: Log.e(TAG, "Failed to add local video track for $peerId")
    }

    fun addAudioTrack(peerId: String) {
        val pc = peers[peerId] ?: return
        if (localAudioTrack == null) startLocalMedia()
        localAudioTrack?.let {
            pc.addTrack(it, listOf(MEDIA_STREAM_ID))
            Log.d(TAG, "Local audio track added to peer connection for $peerId")
        } ?: Log.e(TAG, "Failed to add local audio track for $peerId")
    }

    fun startLocalMedia() {
        if (localVideoTrack != null || localAudioTrack != null) {
            Log.d(TAG, "Local media already started, ignoring request.")
            return
        }
        Log.d(TAG, "Starting local media capture.")

        // Video
        val cameraEnumerator = Camera2Enumerator(context)
        val frontCamera = cameraEnumerator.deviceNames.firstOrNull { cameraEnumerator.isFrontFacing(it) }
        if (frontCamera == null) {
            Log.e(TAG, "No front-facing camera found.")
        } else {
            val cameraEventsHandler = object : CameraVideoCapturer.CameraEventsHandler {
                override fun onCameraError(p0: String?) { Log.e(TAG, "Camera error: $p0") }
                override fun onCameraDisconnected() { Log.w(TAG, "Camera disconnected") }
                override fun onCameraFreezed(p0: String?) { Log.w(TAG, "Camera freezed: $p0") }
                override fun onCameraOpening(p0: String?) { Log.i(TAG, "Opening camera: $p0") }
                override fun onFirstFrameAvailable() { Log.i(TAG, "First frame available from camera") }
                override fun onCameraClosed() { Log.i(TAG, "Camera closed") }
            }
            videoCapturer = cameraEnumerator.createCapturer(frontCamera, cameraEventsHandler)
            videoSource = peerConnectionFactory.createVideoSource(videoCapturer!!.isScreencast)
            val surfaceTextureHelper = SurfaceTextureHelper.create("VideoCapturerThread", eglBase.eglBaseContext)
            videoCapturer!!.initialize(surfaceTextureHelper, context, videoSource!!.capturerObserver)
            videoCapturer!!.startCapture(480, 640, 30) // TODO: Make configurable
            localVideoTrack = peerConnectionFactory.createVideoTrack(VIDEO_TRACK_ID, videoSource).apply {
                localSink?.let { addSink(it) }
            }
        }

        // Audio
        val audioConstraints = MediaConstraints()
        audioSource = peerConnectionFactory.createAudioSource(audioConstraints)
        localAudioTrack = peerConnectionFactory.createAudioTrack(AUDIO_TRACK_ID, audioSource)
        Log.i(TAG, "Local media started successfully.")
    }

    fun stopLocalMedia() {
        Log.d(TAG, "Stopping local media capture.")
        localVideoTrack?.let {
            Log.d(TAG, "Disposing local video track.")
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
        Log.i(TAG, "Local media stopped.")
    }

    fun setLocalSink(sink: VideoSink) {
        Log.d(TAG, "Setting local video sink.")
        localSink?.let { localVideoTrack?.removeSink(it) }
        localSink = sink
        localVideoTrack?.addSink(sink)
    }

    fun setRemoteSink(peerId: String, sink: VideoSink) {
        Log.d(TAG, "Setting remote video sink for peer $peerId.")

        // Remove from old sink if it exists, to prevent memory leaks.
        remoteSinks[peerId]?.let { oldSink ->
            Log.d(TAG, "Removing remote track from old sink for peer $peerId.")
            remoteVideoTracks[peerId]?.removeSink(oldSink)
        }

        remoteSinks[peerId] = sink
        // If the track has already arrived, attach it to the new sink now.
        remoteVideoTracks[peerId]?.let { track ->
            Log.d(TAG, "Track was ready, adding remote track to new sink for peer $peerId.")
            track.addSink(sink)
        }
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
        private const val MEDIA_STREAM_ID = "ARDAMS"
        private const val VIDEO_TRACK_ID = "ARDAMSv0"
        private const val AUDIO_TRACK_ID = "ARDAMSa0"

        // Singleton EglBase instance for the entire application.
        // All WebRTC video components (factory, capturers, renderers) must share this context.
        val eglBase: EglBase by lazy {
            Log.d(TAG, "Creating singleton EglBase.")
            EglBase.create()
        }
    }
}
