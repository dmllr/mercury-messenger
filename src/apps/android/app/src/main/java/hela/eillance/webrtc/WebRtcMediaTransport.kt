package hela.eillance.webrtc

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import androidx.core.content.ContextCompat
import hela.eillance.ndkio.PeerConnectionState
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
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
import org.webrtc.audio.JavaAudioDeviceModule
import java.util.concurrent.ConcurrentHashMap
import kotlin.coroutines.resume

class WebRtcMediaTransport(private val context: Context, private val nativePointer: Long) {

  private class MediaSession(val pc: PeerConnection) {
    val mutex = Mutex()
    var iceGatheringComplete: CompletableDeferred<Unit>? = null

    @Volatile
    var isDisposed = false
    fun dispose() {
      if (isDisposed) return
      isDisposed = true
      try {
        pc.close()
        pc.dispose()
      } catch (_: Exception) {
      }
    }
  }

  private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
  private val eglBase: EglBase = WebRtcTransport.eglBase // Reuse shared EGL

  private var peerConnectionFactory: PeerConnectionFactory? = null
  private val sessions = ConcurrentHashMap<String, MediaSession>()

  // Local Resources
  private var localVideoTrack: VideoTrack? = null
  private var localAudioTrack: AudioTrack? = null
  private var videoCapturer: VideoCapturer? = null
  private var surfaceTextureHelper: SurfaceTextureHelper? = null
  private var videoSource: VideoSource? = null
  private var audioSource: AudioSource? = null
  private var localSink: VideoSink? = null

  private val remoteSinks = ConcurrentHashMap<String, VideoSink>()
  private val remoteVideoTracks = ConcurrentHashMap<String, VideoTrack>()

  private var currentIceServers: List<PeerConnection.IceServer> = listOf()

  private external fun nativeOnPeerConnectionStateChange(
    nativePointer: Long,
    peerId: String,
    context: Int,
    state: Int
  )
  private external fun nativeOnSendMediaSignalingMessage(nativePointer: Long, message: String)

  init {
    peerConnectionFactory = WebRtcTransport.getOrCreateFactory(context)
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
    } catch (_: Exception) {
    }
  }

  @Suppress("unused")
  fun connectPeer(peerId: String) {
    if (sessions.containsKey(peerId)) return
    val factory = peerConnectionFactory ?: return

    val rtcConfig = PeerConnection.RTCConfiguration(currentIceServers).apply {
      sdpSemantics = PeerConnection.SdpSemantics.UNIFIED_PLAN
      bundlePolicy = PeerConnection.BundlePolicy.MAXBUNDLE
      rtcpMuxPolicy = PeerConnection.RtcpMuxPolicy.REQUIRE
      continualGatheringPolicy = PeerConnection.ContinualGatheringPolicy.GATHER_ONCE
    }

    val pcWrapper = arrayOfNulls<PeerConnection>(1)

    val observer = object : PeerConnection.Observer {
      override fun onSignalingChange(state: PeerConnection.SignalingState?) {}
      override fun onConnectionChange(newState: PeerConnection.PeerConnectionState?) {
        val currentSession = sessions[peerId]
        if (currentSession == null || currentSession.pc != pcWrapper[0]) return

        val stateInt = when (newState) {
          PeerConnection.PeerConnectionState.NEW -> PeerConnectionState.New.value
          PeerConnection.PeerConnectionState.CONNECTING -> PeerConnectionState.Connecting.value
          PeerConnection.PeerConnectionState.CONNECTED -> PeerConnectionState.Connected.value
          PeerConnection.PeerConnectionState.DISCONNECTED -> PeerConnectionState.Disconnected.value
          PeerConnection.PeerConnectionState.FAILED -> PeerConnectionState.Failed.value
          PeerConnection.PeerConnectionState.CLOSED -> PeerConnectionState.Closed.value
          null -> PeerConnectionState.Closed.value
        }
        nativeOnPeerConnectionStateChange(nativePointer, peerId, 1, stateInt) // 1=Media
      }

      override fun onIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onStandardizedIceConnectionChange(state: PeerConnection.IceConnectionState?) {}
      override fun onIceConnectionReceivingChange(receiving: Boolean) {}
      override fun onIceGatheringChange(state: PeerConnection.IceGatheringState?) {
        val currentSession = sessions[peerId]
        if (currentSession == null || currentSession.pc != pcWrapper[0]) return
        if (state == PeerConnection.IceGatheringState.COMPLETE) {
          currentSession.iceGatheringComplete?.complete(Unit)
        }
      }

      override fun onIceCandidate(candidate: IceCandidate?) {}
      override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}
      override fun onAddStream(stream: MediaStream?) {}
      override fun onRemoveStream(stream: MediaStream?) {}
      override fun onRenegotiationNeeded() {}
      override fun onDataChannel(dc: DataChannel?) {}
      override fun onTrack(transceiver: RtpTransceiver?) {}
      override fun onAddTrack(receiver: RtpReceiver, streams: Array<out MediaStream>) {
        val currentSession = sessions[peerId]
        if (currentSession == null || currentSession.pc != pcWrapper[0]) return

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
    pcWrapper[0] = pc
    sessions[peerId] = MediaSession(pc)
  }

  fun createOffer(peerId: String) {
    val session = sessions[peerId] ?: return
    scope.launch {
      session.iceGatheringComplete = CompletableDeferred()
      attachLocalMedia(session)

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
        withTimeoutOrNull(1500L) { session.iceGatheringComplete?.await() }
        val finalSdp = session.pc.localDescription ?: offerSdp
        val message = JSONObject().apply {
          put("type", "offer")
          put("sdp", finalSdp.description)
          put("context", "media")
          put("to", peerId)
        }
        nativeOnSendMediaSignalingMessage(nativePointer, message.toString())
      }
    }
  }

  fun createAnswer(peerId: String) {
    val session = sessions[peerId] ?: return
    scope.launch {
      session.iceGatheringComplete = CompletableDeferred()
      attachLocalMedia(session)

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
        withTimeoutOrNull(1500L) { session.iceGatheringComplete?.await() }
        val finalSdp = session.pc.localDescription ?: answerSdp
        val message = JSONObject().apply {
          put("type", "answer")
          put("sdp", finalSdp.description)
          put("context", "media")
          put("to", peerId)
        }
        nativeOnSendMediaSignalingMessage(nativePointer, message.toString())
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

  @Suppress("unused")
  fun getLocalDescription(peerId: String): String? {
    return sessions[peerId]?.pc?.localDescription?.description
  }

  @Suppress("unused")
  fun getPeerConnectionState(peerId: String): Int {
    val state = sessions[peerId]?.pc?.connectionState()
    return when (state) {
      PeerConnection.PeerConnectionState.NEW -> PeerConnectionState.New.value
      PeerConnection.PeerConnectionState.CONNECTING -> PeerConnectionState.Connecting.value
      PeerConnection.PeerConnectionState.CONNECTED -> PeerConnectionState.Connected.value
      PeerConnection.PeerConnectionState.DISCONNECTED -> PeerConnectionState.Disconnected.value
      PeerConnection.PeerConnectionState.FAILED -> PeerConnectionState.Failed.value
      PeerConnection.PeerConnectionState.CLOSED -> PeerConnectionState.Closed.value
      else -> PeerConnectionState.Closed.value
    }
  }

  @Suppress("unused")
  fun disconnectPeer(peerId: String) {
    val session = sessions.remove(peerId)
    if (session != null) {
      scope.launch {
        session.mutex.withLock {
          session.dispose()
        }
      }
    }
    remoteVideoTracks.remove(peerId)
    remoteSinks.remove(peerId)
  }

  @Suppress("unused")
  fun addVideoTrack(peerId: String) {
    startLocalMedia(true)
    sessions[peerId]?.let { attachLocalMedia(it) }
  }

  @Suppress("unused")
  fun addAudioTrack(peerId: String) {
    startLocalMedia(false)
    sessions[peerId]?.let { attachLocalMedia(it) }
  }

  fun startLocalMedia(isVideo: Boolean) {
    if (peerConnectionFactory == null) peerConnectionFactory = WebRtcTransport.getOrCreateFactory(context)
    try {
      if (localAudioTrack == null) {
        val audioConstraints = MediaConstraints()
        audioSource = peerConnectionFactory!!.createAudioSource(audioConstraints)
        localAudioTrack = peerConnectionFactory!!.createAudioTrack("ARDAMSa0", audioSource)
        localAudioTrack?.setEnabled(false) // CRITICAL: Start disabled to prevent hot mic before accept
      }
      if (isVideo && localVideoTrack == null) {
        if (ContextCompat.checkSelfPermission(
            context,
            Manifest.permission.CAMERA
          ) != PackageManager.PERMISSION_GRANTED
        ) return
        val enumerator = Camera2Enumerator(context)
        val deviceName =
          enumerator.deviceNames.firstOrNull { enumerator.isFrontFacing(it) }
            ?: enumerator.deviceNames.firstOrNull()
        if (deviceName != null) {
          videoCapturer = enumerator.createCapturer(deviceName, null)
          val source = peerConnectionFactory!!.createVideoSource(videoCapturer!!.isScreencast)
          videoSource = source
          surfaceTextureHelper =
            SurfaceTextureHelper.create("CaptureThread", eglBase.eglBaseContext)
          videoCapturer!!.initialize(surfaceTextureHelper, context, source.capturerObserver)
          videoCapturer!!.startCapture(640, 480, 30)
          localVideoTrack = peerConnectionFactory!!.createVideoTrack("ARDAMSv0", source)
          localVideoTrack?.setEnabled(false) // CRITICAL: Start disabled
          localSink?.let { localVideoTrack?.addSink(it) }
        }
      }
    } catch (_: Exception) {
    }
  }

  fun stopLocalMedia() {
    // Capture references to dispose in background
    val videoTrackToDispose = localVideoTrack
    val capturerToDispose = videoCapturer
    val textureHelperToDispose = surfaceTextureHelper
    val videoSourceToDispose = videoSource
    val audioTrackToDispose = localAudioTrack
    val audioSourceToDispose = audioSource
    val activeSessions = sessions.values.toList()

    // Null out immediately to prevent re-use
    localVideoTrack = null
    videoCapturer = null
    surfaceTextureHelper = null
    videoSource = null
    localAudioTrack = null
    audioSource = null

    scope.launch {
      localAudioTrack?.setEnabled(false) // Mute immediately

      activeSessions.forEach { session ->
        try {
          session.mutex.withLock {
            if (!session.isDisposed) {
              val senders = session.pc.senders
              for (sender in senders) {
                if (sender.track() != null) session.pc.removeTrack(sender)
              }
            }
          }
        } catch (e: Exception) {
          // Ignore errors during track removal on closing sessions
        }
      }

      try {
        capturerToDispose?.stopCapture()
      } catch (e: Exception) {}

      try {
        videoTrackToDispose?.dispose()
        videoSourceToDispose?.dispose()
        capturerToDispose?.dispose()
        textureHelperToDispose?.dispose()
        audioTrackToDispose?.dispose()
        audioSourceToDispose?.dispose()
      } catch (e: Exception) {
        // Ignore cleanup errors
      }
    }
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

  fun setAudioEnabled(enabled: Boolean) {
    localAudioTrack?.setEnabled(enabled)
  }

  fun setVideoEnabled(enabled: Boolean) {
    localVideoTrack?.setEnabled(enabled)
  }

  private fun attachLocalMedia(session: MediaSession) {
    val senders = session.pc.senders
    localAudioTrack?.let { track ->
      if (senders.none { it.track()?.id() == "ARDAMSa0" }) session.pc.addTrack(
        track,
        listOf("ARDAMS")
      )
    }
    localVideoTrack?.let { track ->
      if (senders.none { it.track()?.id() == "ARDAMSv0" }) session.pc.addTrack(
        track,
        listOf("ARDAMS")
      )
    }
  }
}
