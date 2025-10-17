#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "hela/eillance/common/packet.hpp"
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
  bool isSeiInsertRequired{false};

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

    Log::i("Configuring encoder with dimensions: %dx%d (rotation: %d degrees)",
           config_.width, config_.height, config_.rotation);

    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, config_.width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, config_.height);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, config_.bitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, config_.frameRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 2);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7f000789);  // COLOR_FormatSurface

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

    isSeiInsertRequired = true;
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

    encoded_data.clear();

    auto timeoutUs = Time::uS / config_.frameRate;
    auto index = AMediaCodec_dequeueOutputBuffer(codec, &info, timeoutUs);
    if (likely(index >= 0)) {
      if (unlikely(info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
        buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);
        encoded_data.insert(encoded_data.end(),
                            reinterpret_cast<std::byte*>(&buffer[info.offset]),
                            reinterpret_cast<std::byte*>(&buffer[info.offset + info.size]));
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);

        // on some SDK versions (28. 29) this delay is important before a new AMediaCodec_dequeueOutputBuffer might be called
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        index = AMediaCodec_dequeueOutputBuffer(codec, &info, timeoutUs);
        if (index >= 0) {
          buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);

          encoded_data.insert(encoded_data.end(),
                              reinterpret_cast<std::byte*>(&buffer[info.offset]),
                              reinterpret_cast<std::byte*>(&buffer[info.offset + info.size]));
          AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
        }
      } else {
        buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);
        encoded_data.insert(encoded_data.end(),
                            reinterpret_cast<std::byte*>(&buffer[info.offset]),
                            reinterpret_cast<std::byte*>(&buffer[info.offset + info.size]));
        AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
      }
    } else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
      LOGI << "Output buffers changed";
    } else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      auto format = AMediaCodec_getOutputFormat(codec);
      LOGI << "Format set to: " << AMediaFormat_toString(format);
      AMediaFormat_delete(format);
    } else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
      // it's okay
    } else {
      LOGI << "AMediaCodec_dequeueOutputBuffer unexpected return: " << index;
    }

    // Insert rotation SEI NAL unit if we have valid encoded data and rotation is non-zero
    if (isSeiInsertRequired && !encoded_data.empty()) {
      InsertRotationSei(encoded_data, config_.rotation);
      isSeiInsertRequired = false;
      LOGD << "Inserted SEI NAL with rotation: " << config_.rotation;
    }

    return Packet{encoded_data};
  }

  static std::vector<std::byte> CreateRotationSei(int rotation) {
    std::vector<std::byte> sei;

    // Start code
    sei.push_back(std::byte{0x00});
    sei.push_back(std::byte{0x00});
    sei.push_back(std::byte{0x00});
    sei.push_back(std::byte{0x01});

    // NAL header for SEI (0x06)
    sei.push_back(std::byte{0x06});

    // SEI payload type - user_data_unregistered (5)
    sei.push_back(std::byte{0x05});

    // Construct the SEI message content
    const std::string rotation_text = "rotation-degrees:" + std::to_string(rotation);
    std::vector<std::byte> payload;

    // UUID for Android's rotation metadata (using a common format)
    // Using a placeholder UUID that's recognizable
    const uint8_t uuid[16] = {
        0xD4, 0xD8, 0x92, 0x2E, 0xD0, 0x2E, 0x40, 0xAF,
        0x90, 0xBD, 0x29, 0xE5, 0x06, 0x8D, 0x4C, 0x85
    };

    for (const uint8_t byte : uuid) {
      payload.push_back(std::byte{byte});
    }

    // Add the rotation string
    for (const char c : rotation_text) {
      payload.push_back(std::byte{static_cast<uint8_t>(c)});
    }

    // SEI payload size
    size_t payload_size = payload.size();
    while (payload_size >= 0xFF) {
      sei.push_back(std::byte{0xFF});
      payload_size -= 0xFF;
    }
    sei.push_back(std::byte{static_cast<uint8_t>(payload_size)});

    // Add the payload
    sei.insert(sei.end(), payload.begin(), payload.end());

    // Add trailing bytes for byte alignment (if needed)
    // SEI messages should be byte-aligned
    sei.push_back(std::byte{0x80}); // Trailing bits

    return sei;
  }

  void InsertRotationSei(rtc::binary& data, int rotation) {
    // Create SEI NAL unit with rotation metadata
    auto sei = CreateRotationSei(rotation);

    // Insert the SEI NAL at the beginning of the frame, right after SPS/PPS if present
    // Look for the first non-configuration NAL unit
    size_t pos = 0;
    bool found_first_nal = false;

    // Check for SPS/PPS (NAL types 7 and 8) at the beginning
    while (pos + 4 < data.size()) {
      if ((data[pos] == std::byte{0x00} &&
           data[pos+1] == std::byte{0x00} &&
           data[pos+2] == std::byte{0x00} &&
           data[pos+3] == std::byte{0x01}) ||
          (data[pos] == std::byte{0x00} &&
           data[pos+1] == std::byte{0x00} &&
           data[pos+2] == std::byte{0x01})) {

        // Get NAL unit type
        const size_t header_offset = (data[pos+2] == std::byte{0x01}) ? 3 : 4;
        std::byte nal_type = data[pos + header_offset] & std::byte{0x1F};

        // If we found SPS (7) or PPS (8), continue to the next NAL
        if (nal_type == std::byte{0x07} || nal_type == std::byte{0x08}) {
          // Find the start of the next NAL
          pos++;
          while (pos + 3 < data.size()) {
            if ((data[pos] == std::byte{0x00} &&
                 data[pos+1] == std::byte{0x00} &&
                 data[pos+2] == std::byte{0x00} &&
                 data[pos+3] == std::byte{0x01}) ||
                (data[pos] == std::byte{0x00} &&
                 data[pos+1] == std::byte{0x00} &&
                 data[pos+2] == std::byte{0x01})) {
              break;
            }
            pos++;
          }
        } else {
          // Found a regular NAL, insert SEI before it
          found_first_nal = true;
          break;
        }
      } else {
        pos++;
      }
    }

    // Insert SEI at the appropriate position
    if (found_first_nal) {
      data.insert(data.begin() + pos, sei.begin(), sei.end());
    } else {
      // If no suitable position found, append to the end
      data.insert(data.end(), sei.begin(), sei.end());
    }
  }

};

}  // namespace hela::eillance
