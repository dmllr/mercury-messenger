#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <limits>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

#include "core/rtc/stream.hpp"
#include "rtc/common.hpp"
#include "logging/logging.h"
#include "utils/units.hpp"

#ifdef _WIN32
  #include <combaseapi.h>
#endif

#undef max

class AudioStreamSource : public Stream::Source {
private:
  ma_context context_{};
  ma_device audio_device_{};
  ma_device_config device_config_{ma_device_config_init(ma_device_type_capture)};

  const AVCodec* codec_{};
  AVCodecContext* codecContext_{};
  AVFrame* frame_{nullptr};
  AVPacket* packet_{nullptr};

  rtc::binary encoded_data_{};
  std::chrono::time_point<std::chrono::steady_clock> start_time_;
  std::uint64_t sample_time_{std::numeric_limits<std::uint64_t>::max()};

public:
  AudioStreamSource() {
    // audio device
    device_config_.capture.format = ma_format_f32;
    device_config_.capture.channels = 2;  // stereo
    device_config_.sampleRate = 48000;
    device_config_.pUserData = this;
    device_config_.dataCallback = [](ma_device* device, void*, const void* frames, ma_uint32 frame_count) {
      auto* self = static_cast<AudioStreamSource*>(device->pUserData);
      if (self != nullptr) {
        self->EncodeBuffer(frames, frame_count); // NOLINT(*-narrowing-conversions)
      }
    };
  }

  void Start() final {
    auto result = ma_context_init(nullptr, 0, nullptr, &context_);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to initialize Miniaudio context";
      return;
    }

    // Open the audio device
    result = ma_device_init(&context_, &device_config_, &audio_device_);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to open audio device";
      ma_context_uninit(&context_);
      return;
    }

    // Start the audio device
    result = ma_device_start(&audio_device_);
    if (result != MA_SUCCESS) {
      LOGE << "Failed to start audio device";
      ma_device_uninit(&audio_device_);
      ma_context_uninit(&context_);
      return;
    }

    // Find encoder
    codec_ = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    if (codec_ == nullptr) {
      LOGE << "Codec not found";
      return;
    }

    // Create codec context
    codecContext_ = avcodec_alloc_context3(codec_);
    if (codecContext_ == nullptr) {
      LOGE << "Failed to allocate codec context";
      return;
    }

    // Set codec options
    assert(device_config_.capture.format == ma_format_f32);
    codecContext_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    codecContext_->bit_rate = 64000; // TODO: take from UI a desired bit rate
    codecContext_->sample_rate = device_config_.sampleRate; // NOLINT(*-narrowing-conversions)
    av_channel_layout_default(&codecContext_->ch_layout, device_config_.capture.channels); // NOLINT(*-narrowing-conversions)

    // Open codec
    AVDictionary* options = nullptr;
    av_dict_set(&options, "strict", "-2", 0);
    if (avcodec_open2(codecContext_, codec_, &options) < 0) {
      LOGE << "Failed to open codec";
      return;
    }

    // Allocate frame
    frame_ = av_frame_alloc();
    if (frame_ == nullptr) {
      LOGE << "Failed to allocate frame";
      return;
    }

    // Set frame parameters
    frame_->format = codecContext_->sample_fmt;
    frame_->ch_layout = codecContext_->ch_layout;

    packet_ = av_packet_alloc();

    start_time_ = std::chrono::steady_clock::now();
    sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_).count();
  }

  void LoadNextSample() final {
    sample_time_ = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time_).count();
  }

  auto GetSample() -> rtc::binary final {
    auto sample = encoded_data_;
    encoded_data_.clear();

    LOGV << "Sending audio sample of size " << sample.size() << "b ";
    return sample;
  }

  void Stop() final {
    // Cleanup ffmpeg codec
    if (packet_ != nullptr) {
      av_packet_free(&packet_);
    }
    if (codecContext_ != nullptr) {
      avcodec_free_context(&codecContext_);
    }
    if (frame_ != nullptr) {
      av_frame_free(&frame_);
    }


    // Stop and uninitialize the audio device
    ma_device_uninit(&audio_device_);

    // uninitialize the miniaudio context
    ma_context_uninit(&context_);
  }

  auto GetSampleTime() -> uint64_t final {
    return sample_time_;
  }

private:
  void EncodeBuffer(const void* frames, int frame_count) {
    const auto buffer_size = av_samples_get_buffer_size(nullptr, codecContext_->ch_layout.nb_channels, frame_count, codecContext_->sample_fmt, 0);

    codecContext_->frame_size = frame_count; // NOLINT(*-narrowing-conversions)
    frame_->nb_samples = frame_count; // NOLINT(*-narrowing-conversions)

    // Allocate temporary buffer for FFmpeg
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(buffer_size)); // TODO buffer to class variable
    if (buffer == nullptr) {
      LOGE << "Failed to allocate buffer";
      return;
    }

    // Fill frame data
    auto ret = avcodec_fill_audio_frame(frame_, codecContext_->ch_layout.nb_channels, codecContext_->sample_fmt, static_cast<const std::uint8_t*>(frames), buffer_size, 0);
    if (ret < 0) {
      LOGE << "Error filling audio frame";
      return;
    }
    frame_->pts = av_gettime_relative() / Units::Milliseconds; // NOLINT(*-narrowing-conversions)

    // Encode frame
    ret = avcodec_send_frame(codecContext_, frame_);
    if (ret < 0) {
      LOGE << "Error sending frame to codec";
      return;
    }

    while (ret >= 0) {
      ret = avcodec_receive_packet(codecContext_, packet_);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      }
      if (ret < 0) {
        LOGE << "Error encoding frame";
        return;
      }

      encoded_data_.insert(
          encoded_data_.end(),
          reinterpret_cast<std::byte*>(packet_->data),
          reinterpret_cast<std::byte*>(std::next(packet_->data, packet_->size)));

      av_packet_unref(packet_);
    }

    av_free(buffer);
  }

};
