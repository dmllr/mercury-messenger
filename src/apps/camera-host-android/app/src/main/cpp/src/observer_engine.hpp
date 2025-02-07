#pragma once

#include <jni.h>
#include <string>
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

namespace hela::eillance {

class ObserverEngine {
  ANativeWindow* nativeWindow_{nullptr};
  AMediaCodec* mediaCodec_{nullptr};
  bool codecConfigured_{false};

  hela::eillance::RtcConsumer consumer_{};
  hela::eillance::H264Parser h264Parser_{};

public:
  ObserverEngine() = default;
  ObserverEngine(ObserverEngine&&) = delete;
  ObserverEngine(const ObserverEngine&) = delete;
  auto& operator=(ObserverEngine&&) = delete;
  auto& operator=(const ObserverEngine&) = delete;

  ~ObserverEngine() {
    Stop();
  }

  void SetTargetSurface(JNIEnv* env, jobject surface) {
    nativeWindow_ = ANativeWindow_fromSurface(env, surface);
  }

  void Start() {
    InitH264Parser();
    consumer_.SetName("CLI::" + std::string(StringUtils::RandomString()));
    consumer_.Start();
    consumer_.SetClientAddedCallback([this]() {
      consumer_.SendStreamRequest();
    });
    consumer_.SetFrameReceivedCallback([this](const rtc::binary& frame) {
      h264Parser_.ProcessFrame(frame);
    });
    LOGI << "Decoder initialized and ready.";
  }

  void Stop() {
    consumer_.Stop();
    if (mediaCodec_ != nullptr) {
      AMediaCodec_stop(mediaCodec_);
      AMediaCodec_delete(mediaCodec_);
      mediaCodec_ = nullptr;
    }
    if (nativeWindow_ != nullptr) {
      ANativeWindow_release(nativeWindow_);
      nativeWindow_ = nullptr;
    }
    codecConfigured_ = false;
    LOGI << "Decoder resources released.";
  }

private:
  /// @brief Initializes the codec with @c VideoConfig.
  void ConfigureCodec(const VideoConfig& conf) {
    if (codecConfigured_) {
      LOGI << "Codec already configured, skipping reconfiguration.";
      return;
    }

    AMediaFormat* format = AMediaFormat_new();
    if (format == nullptr) {
      LOGE << "Failed to create AMediaFormat.";
      return;
    }

    const auto color_format = 0x7f000789;  // COLOR_FormatSurface
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, static_cast<std::int32_t>(conf.width));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<std::int32_t>(conf.height));
    AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_COLOR_FORMAT, color_format);  // COLOR_FormatSurface

    mediaCodec_ = AMediaCodec_createDecoderByType("video/avc");
    if (mediaCodec_ == nullptr) {
      LOGE << "Failed to create AMediaCodec.";
      AMediaFormat_delete(format);
      return;
    }

    if (AMediaCodec_configure(mediaCodec_, format, nativeWindow_, nullptr, 0) != AMEDIA_OK) {
      LOGE << "AMediaCodec configuration failed.";
      AMediaCodec_delete(mediaCodec_);
      mediaCodec_ = nullptr;
    } else {
      AMediaCodec_start(mediaCodec_);
      codecConfigured_ = true;
      LOGI << "MediaCodec successfully configured.";
    }

    AMediaFormat_delete(format);
  }

  /// @brief Handles output buffers and renders frames to nativeWindow_.
  void HandleOutputBuffer() {
    if (!codecConfigured_ || mediaCodec_ == nullptr) {
      return;
    }

    AMediaCodecBufferInfo info;
    while (true) {
      const auto output_buffer_index = AMediaCodec_dequeueOutputBuffer(mediaCodec_, &info, 0);

      if (output_buffer_index >= 0) {
        // Render the buffer to nativeWindow_
        AMediaCodec_releaseOutputBuffer(mediaCodec_, output_buffer_index, true);

      } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        LOGI << "Output buffers changed.";

      } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mediaCodec_);
        LOGI << "Output format changed: " << AMediaFormat_toString(newFormat);
        AMediaFormat_delete(newFormat);

      } else if (output_buffer_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        // No available output buffer, exit the loop
        break;
      }
    }
  }

  /// @brief Sends the parsed frame data to the codec.
  void HandleInputBuffer(const std::vector<std::byte>& frameBuffer) {
    if (!codecConfigured_) {
      LOGW << "Codec not configured yet, dropping frame.";
      return;
    }

    int buf_index = AMediaCodec_dequeueInputBuffer(mediaCodec_, 10000); // 10ms timeout
    if (buf_index < 0) {
      LOGW << "No available input buffer.";
      HandleOutputBuffer();
      return;
    }

    size_t buf_size = 0;
    auto *buf = AMediaCodec_getInputBuffer(mediaCodec_, buf_index, &buf_size);
    if (buf == nullptr) {
      LOGW << "Failed to get input buffer.";
      return;
    }

    const auto copy_size = std::min(frameBuffer.size(), buf_size);
    std::memcpy(buf, frameBuffer.data(), copy_size);
    AMediaCodec_queueInputBuffer(mediaCodec_, buf_index, 0, copy_size, 0, 0);

    // Process output buffers immediately after input
    HandleOutputBuffer();
  }

  /// @brief Initializes the H.264 parser and sets up callbacks.
  void InitH264Parser() {
    h264Parser_.SetOnConfigureCallback([this](const VideoConfig& conf) {
      LOGI << "Received SPS/PPS, configuring codec...";
      ConfigureCodec(conf);
    });

    h264Parser_.SetOnInputBufferCallback([this](const std::vector<std::byte>& frameBuffer) {
      HandleInputBuffer(frameBuffer);
    });
  }
};

}  // namespace hela::eillance
