package hela.eillance.webrtc

import android.content.Context
import org.webrtc.DefaultVideoDecoderFactory
import org.webrtc.DefaultVideoEncoderFactory
import org.webrtc.EglBase
import org.webrtc.PeerConnectionFactory
import org.webrtc.audio.JavaAudioDeviceModule

/**
 * Shared resources for WebRTC data and media transports.
 */
class WebRtcTransport {
  companion object {
    val eglBase: EglBase by lazy { EglBase.create() }

    @Volatile
    private var peerConnectionFactory: PeerConnectionFactory? = null

    @Volatile
    var audioDeviceModule: JavaAudioDeviceModule? = null

    fun getOrCreateFactory(context: Context): PeerConnectionFactory {
      return peerConnectionFactory ?: synchronized(this) {
        peerConnectionFactory ?: run {
          PeerConnectionFactory.initialize(
            PeerConnectionFactory.InitializationOptions.builder(context.applicationContext)
              .setEnableInternalTracer(false)
              .createInitializationOptions()
          )

          audioDeviceModule = JavaAudioDeviceModule.builder(context.applicationContext)
            .setUseHardwareAcousticEchoCanceler(true)
            .setUseHardwareNoiseSuppressor(true)
            .createAudioDeviceModule()

          val factory = PeerConnectionFactory.builder()
            .setAudioDeviceModule(audioDeviceModule)
            .setVideoEncoderFactory(DefaultVideoEncoderFactory(eglBase.eglBaseContext, true, true))
            .setVideoDecoderFactory(DefaultVideoDecoderFactory(eglBase.eglBaseContext))
            .createPeerConnectionFactory()

          peerConnectionFactory = factory
          factory
        }
      }
    }
  }
}
