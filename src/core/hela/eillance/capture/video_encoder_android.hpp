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

  AMediaCodec* codec{nullptr};
  ANativeWindow* surface{nullptr};
  CodecStatus status{UNKNOWN};

  AMediaCodecBufferInfo info{};
  uint8_t* buffer{};
  rtc::binary encoded_data{};

public:
  ANativeWindow* Surface() {
    return surface;
  }

  void Engage() {
    if (status == STARTED)
      return;

    codec = AMediaCodec_createEncoderByType("video/avc");
    Log::assert_or_die(static_cast<bool>(codec), "There is no 'video/avc' codec support available");

    auto format = AMediaFormat_new();

    auto width = 1920;
    auto height = 1080;
    auto frameRate = 30;
    auto bitRate = static_cast<int>(0.22f * width * height * frameRate);
    auto i_frame = 30;
    auto colorFormat = 0x7f000789;  // COLOR_FormatSurface, https://developer.android.com/reference/android/media/MediaCodecInfo.CodecCapabilities#COLOR_FormatSurface
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
    auto profile = 0x00080000;  // AVCProfileConstrainedHigh, https://developer.android.com/reference/android/media/MediaCodecInfo.CodecProfileLevel#AVCProfileConstrainedBaseline
    auto level = 0x200;         // AVCLevel3, https://developer.android.com/reference/android/media/MediaCodecInfo.CodecProfileLevel
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_PROFILE, profile);
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_LEVEL, level);
    rc = AMediaCodec_setParameters(codec, params);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_setParameters failed");
    AMediaFormat_delete(params);

    rc = AMediaCodec_createInputSurface(codec, &surface);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_createInputSurface failed");

    rc = AMediaCodec_start(codec);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_start failed");
    status = STARTED;
  }

  void Revoke() {
    if (status == STOPPED)
      return;

    auto rc = AMediaCodec_stop(codec);
    Log::assert_soft(AMEDIA_OK == rc, "AMediaCodec_stop failed");
    status = STOPPED;
  }

  Packet EncodeBuffer() {
    auto index = AMediaCodec_dequeueOutputBuffer(codec, &info, 1000LL);  // TODO const timeout
    if (likely(index >= 0)) {
      if (unlikely(info.flags == AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
        const auto config_info = info;
        const auto config_index = index;
        const auto config_buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(config_index), NULL);
        index = AMediaCodec_dequeueOutputBuffer(codec, &info, 1000LL);  // TODO const timeout
        buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);

        encoded_data.clear();
        encoded_data.insert(encoded_data.end(),
                            reinterpret_cast<std::byte*>(&config_buffer[config_info.offset]),
                            reinterpret_cast<std::byte*>(&config_buffer[config_info.offset + config_info.size]));
        encoded_data.insert(encoded_data.end(),
                            reinterpret_cast<std::byte*>(&buffer[info.offset]),
                            reinterpret_cast<std::byte*>(&buffer[info.size]));

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
      LOGW << "No output buffer right now";
      encoded_data.clear();
    } else {
      LOGI << "AMediaCodec_dequeueOutputBuffer unexpected return: " << index;
      encoded_data.clear();
    }

    return Packet{encoded_data,1};
  }
};

}  // namespace hela::eillance
