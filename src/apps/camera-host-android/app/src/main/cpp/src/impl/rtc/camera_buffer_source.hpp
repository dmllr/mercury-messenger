#pragma once

#include <fstream>
#include <string>
#include <vector>

#include "core/rtc/stream.hpp"


class CameraBufferSource : public StreamSource {
  static constexpr auto target = CaptureTarget::RTC_ENCODER_SURFACE;

  AMediaCodecBufferInfo info;
  uint8_t* buffer = nullptr;
  //	uint64_t timeStart_us = 0;
  std::chrono::time_point<std::chrono::steady_clock> start_time;
  rtc::binary encoded_data;

protected:
  std::shared_ptr<CameraManager> camera_manager;

public:
  CameraBufferSource(std::shared_ptr<CameraManager> camera_manager) : camera_manager(camera_manager) {
  }

  virtual void start() override {
    camera_manager->codec()->start();
    camera_manager->addConsumer(target);
    //		loadNextSample();

    start_time = std::chrono::steady_clock::now();
  }

  virtual void stop() override {
    camera_manager->removeConsumer(target);
    camera_manager->codec()->stop();
  }

  virtual void loadNextSample() override {
    const auto& codec = camera_manager->codec()->getCodec();
    auto index = AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);  // TODO const timeout
    if (likely(index >= 0)) {
      if (unlikely(info.flags == AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG)) {
        const auto config_info = info;
        const auto config_index = index;
        const auto config_buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(config_index), NULL);
        index = AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);  // TODO const timeout
        buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), NULL);

        //				timeStart_us = info.presentationTimeUs;

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
  }

  rtc::binary getSample() override {
    auto sample = encoded_data;
    encoded_data.clear();

    LOGV << "Sending sample of size " << sample.size() << "b " << std::endl;

    return sample;
  }

  uint64_t getSampleTime_us() override {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
  }

  uint64_t getSampleDuration_us() override {
    return 1000000000 / 30;  // todo;
  }
};
