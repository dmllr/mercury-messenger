#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/logging/logging.h"

namespace hela::eillance {

class VideoEncoder {
  enum CodecStatus {
    UNKNOWN = 0,
    STARTED,
    STOPPED
  };

  VideoConfig config_;

  AMediaCodec* codec{nullptr};
  ANativeWindow* surface{nullptr};
  CodecStatus status{UNKNOWN};

  AMediaCodecBufferInfo info{};
  uint8_t* buffer{};
  rtc::binary encoded_data{};

  void configureCodec() {
    if (codec != nullptr) {
      auto rc = AMediaCodec_stop(codec);
      Log::assert_soft(AMEDIA_OK == rc, "AMediaCodec_stop failed");
      codec = nullptr;
    }

    codec = AMediaCodec_createEncoderByType("video/avc");
    Log::assert_or_die(static_cast<bool>(codec), "There is no 'video/avc' codec support available");

    auto format = AMediaFormat_new();

    auto width = config_.width;
    auto height = config_.height;
    auto frameRate = config_.frameRate;
    auto bitRate = config_.bitrate;
    auto i_frame = 30;
    auto colorFormat = 0x7f000789;  // COLOR_FormatSurface
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, bitRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, frameRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, i_frame);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, colorFormat);
    auto rc = AMediaCodec_configure(codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_configure failed");
    AMediaFormat_delete(format);

    auto params = AMediaFormat_new();
    auto profile = 0x00080000;  // AVCProfileConstrainedHigh
    auto level = 0x200;         // AVCLevel3
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_PROFILE, profile);
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_LEVEL, level);
    rc = AMediaCodec_setParameters(codec, params);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_setParameters failed");
    AMediaFormat_delete(params);

    if (surface != nullptr) {
      ANativeWindow_release(surface);
      surface = nullptr;
    }

    rc = AMediaCodec_createInputSurface(codec, &surface);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_createInputSurface failed");

    rc = AMediaCodec_start(codec);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_start failed");
  }

public:
  ANativeWindow* Surface() {
    return surface;
  }

  void Engage(VideoConfig config) {
    if (status == STARTED)
      return;

    config_ = config;
    configureCodec();
    status = STARTED;
  }

  void Revoke() {
    if (status == STOPPED)
      return;

    if (codec != nullptr) {
      auto rc = AMediaCodec_stop(codec);
      Log::assert_soft(AMEDIA_OK == rc, "AMediaCodec_stop failed");
      codec = nullptr;
    }
    if (surface != nullptr) {
      ANativeWindow_release(surface);
      surface = nullptr;
    }
    status = STOPPED;
  }

  void UpdateConfig(VideoConfig new_config) {
    config_ = new_config;
    if (status == STARTED) {
      auto rc = AMediaCodec_flush(codec);
      Log::assert_soft(AMEDIA_OK == rc, "AMediaCodec_flush failed");
      configureCodec();
    }
  }

  Packet EncodeBuffer() {
    if (unlikely(codec == nullptr || surface == nullptr)) {
      return {};
    }

    auto timeoutUs = Time::uS / config_.frameRate;
    auto index = AMediaCodec_dequeueOutputBuffer(codec, &info, timeoutUs);
    if (likely(index >= 0)) {
      if (unlikely(info.flags == AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
        const auto config_info = info;
        const auto config_index = index;
        const auto config_buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(config_index), NULL);
        index = AMediaCodec_dequeueOutputBuffer(codec, &info, timeoutUs);
        buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);

        if (likely(buffer != nullptr)) {
          encoded_data.clear();
          encoded_data.insert(encoded_data.end(),
                              reinterpret_cast<std::byte*>(&config_buffer[config_info.offset]),
                              reinterpret_cast<std::byte*>(&config_buffer[config_info.offset + config_info.size]));
          encoded_data.insert(encoded_data.end(),
                              reinterpret_cast<std::byte*>(&buffer[info.offset]),
                              reinterpret_cast<std::byte*>(&buffer[info.size]));
        }

        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(config_index), false);
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
      } else {
        buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);
        encoded_data.clear();
        encoded_data.insert(encoded_data.end(),
                            reinterpret_cast<std::byte*>(&buffer[info.offset]),
                            reinterpret_cast<std::byte*>(&buffer[info.size]));
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
      }
    } else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      LOGI << "Output buffers changed";
      encoded_data.clear();
    } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      auto format = AMediaCodec_getOutputFormat(codec);
      LOGI << "Format set to: " << AMediaFormat_toString(format);
      AMediaFormat_delete(format);
      encoded_data.clear();
    } else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      // it's okay
    } else {
      LOGI << "AMediaCodec_dequeueOutputBuffer unexpected return: " << index;
      encoded_data.clear();
    }

    return Packet{std::move(encoded_data)};
  }
};

}  // namespace hela::eillance
