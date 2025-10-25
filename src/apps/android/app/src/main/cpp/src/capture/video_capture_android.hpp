#pragma once

#include "eillance/utils/time.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/utils/packet.hpp"
#include "video_encoder_android.hpp"

namespace eillance {

// This is a stub implementation for Android.
// Video capture is handled by the WebRtcTransport in Kotlin.
class VideoCaptureAndroid {
public:
  VideoCaptureAndroid() = default;
  ~VideoCaptureAndroid() = default;
  VideoCaptureAndroid(VideoCaptureAndroid&&) = delete;
  VideoCaptureAndroid(const VideoCaptureAndroid&) = delete;
  VideoCaptureAndroid& operator=(VideoCaptureAndroid&&) = delete;
  VideoCaptureAndroid& operator=(const VideoCaptureAndroid&) = delete;

  void Engage() {}
  void Revoke() {}
  Packet FetchBuffers() { return {}; }
  [[nodiscard]] Time::Unit GetSampleDuration() const { return 1000000 / 30; } // 30 fps
  void Configure(const Config& /*config*/) {}
  void ConfigureVideo(const VideoConfig& /*new_config*/) {}
};

}  // namespace eillance
