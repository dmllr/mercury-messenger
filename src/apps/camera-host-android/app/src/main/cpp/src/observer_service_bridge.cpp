#include <jni.h>

#include <string>
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>

#include <rtc/rtc.hpp>

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/rtc/consumer.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/string.hpp"
#include "hela/eillance/utils/h264.hpp"

ANativeWindow* nativeWindow = nullptr;
AMediaCodec* mediaCodec = nullptr;
bool codecConfigured = false;
hela::eillance::RtcConsumer consumer{};
hela::eillance::H264Parser h264Parser;
using VideoConfig = hela::eillance::VideoConfig;

/// @brief Initializes the codec with SPS/PPS.
void ConfigureCodec(const VideoConfig& conf, const std::vector<std::byte>& sps, const std::vector<std::byte>& pps) {
  if (codecConfigured) {
    LOGI << "Codec already configured, skipping reconfiguration.";
    return;
  }

  // Convert std::vector<std::byte> to std::vector<uint8_t>
  std::vector<uint8_t> sps_bytes(sps.size());
  std::vector<uint8_t> pps_bytes(pps.size());

  std::memcpy(sps_bytes.data(), sps.data(), sps.size());
  std::memcpy(pps_bytes.data(), pps.data(), pps.size());

  AMediaFormat* format = AMediaFormat_new();
  if (format == nullptr) {
    LOGE << "Failed to create AMediaFormat.";
    return;
  }

  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
  AMediaFormat_setBuffer(format, "csd-0", sps_bytes.data(), sps_bytes.size());
  AMediaFormat_setBuffer(format, "csd-1", pps_bytes.data(), pps_bytes.size());
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, static_cast<std::int32_t>(conf.width));
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<std::int32_t>(conf.height));

  mediaCodec = AMediaCodec_createDecoderByType("video/avc");
  if (mediaCodec == nullptr) {
    LOGE << "Failed to create AMediaCodec.";
    AMediaFormat_delete(format);
    return;
  }

  if (AMediaCodec_configure(mediaCodec, format, nativeWindow, nullptr, 0) != AMEDIA_OK) {
    LOGE << "AMediaCodec configuration failed.";
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  } else {
    AMediaCodec_start(mediaCodec);
    codecConfigured = true;
    LOGI << "MediaCodec successfully configured.";
  }

  AMediaFormat_delete(format);
}

/// @brief Sends the parsed frame data to the codec.
void HandleInputBuffer(const std::vector<std::byte>& frameBuffer) {
  if (!codecConfigured) {
    LOGW << "Codec not configured yet, dropping frame.";
    return;
  }

  const auto buf_index = AMediaCodec_dequeueInputBuffer(mediaCodec, 0);
  if (buf_index < 0) {
    LOGW << "No available input buffer.";
    return;
  }

  size_t buf_size = 0;
  auto *buf = AMediaCodec_getInputBuffer(mediaCodec, buf_index, &buf_size);
  if (buf == nullptr) {
    LOGW << "Failed to get input buffer.";
    return;
  }

  const auto copy_size = std::min(frameBuffer.size(), buf_size);
  std::memcpy(buf, frameBuffer.data(), copy_size);
  AMediaCodec_queueInputBuffer(mediaCodec, buf_index, 0, copy_size, 0, 0);
}

/// @brief Initializes the H.264 parser and sets up callbacks.
void InitH264Parser() {
  h264Parser.SetOnConfigureCallback(
      [](
          const VideoConfig& conf,
          const std::vector<std::byte>& sps,
          const std::vector<std::byte>& pps) {
    LOGI << "Received SPS/PPS, configuring codec...";
    ConfigureCodec(conf, sps, pps);
  });

  h264Parser.SetOnInputBufferCallback([](const std::vector<std::byte>& frameBuffer) {
    HandleInputBuffer(frameBuffer);
  });
}

/// @brief JNI function to initialize the decoder with a SurfaceTexture from Kotlin.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject surface) {
  if (nativeWindow != nullptr) {
    ANativeWindow_release(nativeWindow);
  }
  nativeWindow = ANativeWindow_fromSurface(env, surface);
  if (nativeWindow == nullptr) {
    LOGE << "Failed to obtain ANativeWindow from Surface.";
    return;
  }

  InitH264Parser();

  consumer.SetName("CLI::" + std::string(StringUtils::RandomString()));
  consumer.Start();
  consumer.SetClientAddedCallback([]() {
    consumer.SendStreamRequest();
  });
  consumer.SetFrameReceivedCallback([](const rtc::binary& frame) {
    h264Parser.ProcessFrame(frame);
  });

  LOGI << "Decoder initialized and ready.";
}

/// @brief JNI function to release decoder resources.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_releaseDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv*, jobject) {
  consumer.Stop();
  if (mediaCodec != nullptr) {
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (nativeWindow != nullptr) {
    ANativeWindow_release(nativeWindow);
    nativeWindow = nullptr;
  }
  codecConfigured = false;
  LOGI << "Decoder resources released.";
}
