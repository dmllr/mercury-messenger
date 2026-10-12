package hela.eillance.webrtc

import org.webrtc.EglBase

/**
 * Shared resources for WebRTC data and media transports.
 */
class WebRtcTransport {
  companion object {
    val eglBase: EglBase by lazy { EglBase.create() }
  }
}
