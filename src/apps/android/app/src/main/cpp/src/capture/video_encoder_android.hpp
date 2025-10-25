#pragma once

#include <media/NdkMediaCodec.h>
#include "eillance/utils/packet.hpp"
#include "eillance/capture/video_config.hpp"

namespace eillance {

// This is a stub implementation for Android.
// Video encoding is handled by the WebRtcTransport in Kotlin.
class VideoEncoderAndroid {
public:
  ANativeWindow* Surface() { return nullptr; }
  void Engage(const VideoConfig& /*config*/) {}
  void Revoke() {}
  void UpdateConfig(const VideoConfig& /*new_config*/) {}
  Packet EncodeBuffer() { return {}; }
};

}  // namespace eillance
