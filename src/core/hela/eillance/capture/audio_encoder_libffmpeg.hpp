#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}

#include "hela/eillance/capture/audio_config.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/time.hpp"
#include "hela/eillance/utils/units.hpp"

namespace hela::eillance {

class AudioEncoder {
  const AVCodec* codec_{};
  AVCodecContext* codecContext_{};
  AVFrame* frame_{nullptr};
  AVPacket* packet_{nullptr};

public:
  void Engage() {
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
    codecContext_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    codecContext_->bit_rate = 64000;                       // TODO: take from UI a desired bit rate
    codecContext_->sample_rate = AudioConfig::sampleRate;  // NOLINT(*-narrowing-conversions)
    av_channel_layout_default(&codecContext_->ch_layout, AudioConfig::channels);  // NOLINT(*-narrowing-conversions)

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
  }

  void Revoke() {
    // Cleanup ffmpeg codec
    if (packet_ != nullptr) {
      av_packet_free(&packet_);
    }
    if (frame_ != nullptr) {
      av_frame_free(&frame_);
    }
    if (codecContext_ != nullptr) {
      avcodec_free_context(&codecContext_);
    }
  }

  Packet EncodeBuffer(Packet raw_buffers) {
    codecContext_->frame_size = raw_buffers.amount; // NOLINT(*-narrowing-conversions)
    frame_->nb_samples = codecContext_->frame_size;

    frame_->pts = Time::Now();
    auto ret = avcodec_fill_audio_frame(
        frame_,
        codecContext_->ch_layout.nb_channels,
        codecContext_->sample_fmt,
        reinterpret_cast<const std::uint8_t*>(raw_buffers.data.data()),
        static_cast<int>(raw_buffers.data.size()), // NOLINT(*-narrowing-conversions)
        0);
    if (ret < 0) {
      LOGE << "Error " << ret << " filling audio frame";
      return {};
    }
    assert(ret == raw_buffers.data.size() && "Input buffer size and AV frame size are not equal!");

    // Encode frame
    ret = avcodec_send_frame(codecContext_, frame_);
    if (ret < 0) {
      LOGE << "Error " << ret << " encoding frame in avcodec_send_frame.";
      return {};
    }
    ret = avcodec_receive_packet(codecContext_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return {};
    }
    if (ret < 0) {
      LOGE << "Error " << ret << " encoding frame in avcodec_receive_packet.";
      return {};
    }

    auto result = Packet{
            std::vector(
                reinterpret_cast<std::byte*>(packet_->data),
                reinterpret_cast<std::byte*>(std::next(packet_->data, packet_->size))),
            raw_buffers.amount};
    av_packet_unref(packet_);
    return result;
  }

};

}  // namespace hela::eillance
