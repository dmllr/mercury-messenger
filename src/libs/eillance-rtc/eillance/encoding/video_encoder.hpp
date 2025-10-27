#pragma once

#include <array>
#include <utility>
#include <vector>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavcodec/codec.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/packet.h"
#include "libavutil/error.h"
#include "libavutil/frame.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
}
#ifdef _WIN32
#undef CLSCTX_ALL
#include <combaseapi.h>
#endif

#include "eillance/logging/logging.h"
#include "eillance/utils/packet.hpp"
#include "eillance/utils/time.hpp"

namespace eillance {

class VideoEncoderCli {
  AVCodecContext* codec_context_ = nullptr;
  AVFrame* av_frame_ = nullptr;
  AVPacket* packet_ = nullptr;
  std::vector<uint8_t> converted_frame_buffer_;

#if defined __ANDROID__
  static constexpr auto codecDestPixelFormat = AV_PIX_FMT_NV12;
#else
  static constexpr auto codecDestPixelFormat = AV_PIX_FMT_YUV420P;
#endif

  // List of preferred codecs in priority order
  static constexpr std::array<AVCodecID, 1> preferred_codecs = {
      AV_CODEC_ID_H264,  // try H.264 codec first
  };

  bool SetupCodecContext(const AVCodec* codec, int width, int height, int framerate) {
    codec_context_ = avcodec_alloc_context3(codec);
    if (codec_context_ == nullptr) {
      return false;
    }

    codec_context_->width = width;
    codec_context_->height = height;
    codec_context_->time_base = AVRational{1, framerate};
    codec_context_->framerate = AVRational{framerate, 1};
    codec_context_->thread_count = 4;  // Using 4 threads for efficient CPU usage
    codec_context_->gop_size = 50;     // Larger GOP size for reduced CPU load
    codec_context_->max_b_frames = 0;  // No B-frames for lower latency
    codec_context_->pix_fmt = codecDestPixelFormat;

#ifdef _WIN32
    // Disable hardware encoding on Windows if needed
    av_opt_set(codec_context_->priv_data, "hw_encoding", "0", 0);
#endif
    return avcodec_open2(codec_context_, codec, nullptr) >= 0;
  }

  void AllocateFrame(int width, int height) {
    av_frame_ = av_frame_alloc();
    if (av_frame_ == nullptr) {
      throw std::runtime_error("Failed to allocate frame");
    }

    const auto num_bytes = av_image_get_buffer_size(codec_context_->pix_fmt, width, height, 1);
    converted_frame_buffer_.resize(num_bytes);
    av_image_fill_arrays(av_frame_->data, av_frame_->linesize, converted_frame_buffer_.data(),
                         codec_context_->pix_fmt, width, height, 1);

    av_frame_->width = width;
    av_frame_->height = height;
    av_frame_->format = codec_context_->pix_fmt;

    packet_ = av_packet_alloc();
  }

public:
  void Engage(const int width, const int height, const int framerate) {
#ifdef _WIN32
    CoUninitialize();
#endif
    const AVCodec* codec = nullptr;
    for (const auto& codec_id: preferred_codecs) {
      codec = avcodec_find_encoder(codec_id);
      if (codec && SetupCodecContext(codec, width, height, framerate)) {
        LOGD << "Using codec: [" << codec->id << "] " << codec->name << " | " << codec->long_name;
        break;
      }
    }

    // Fallback to software encoder if hardware encoders fail
    if (codec_context_ == nullptr) {
      codec = avcodec_find_encoder(AV_CODEC_ID_H264);
      if (!SetupCodecContext(codec, width, height, framerate)) {
        throw std::runtime_error("Failed to initialize any encoder.");
      }
      LOGD << "Falling back to software codec: [" << codec->id << "] " << codec->name;
    }

    AllocateFrame(width, height);
  }

  void Revoke() {
    av_frame_free(&av_frame_);
    av_packet_free(&packet_);
    avcodec_free_context(&codec_context_);
#ifdef _WIN32
    CoUninitialize();
#endif
  }

  Packet EncodeBuffer(void* frames) const {
    if (codec_context_ == nullptr) {
      return {};
    }

    const auto size = codec_context_->width * codec_context_->height;
    av_frame_->data[0] = static_cast<std::uint8_t*>(frames);
    av_frame_->data[1] = std::next(av_frame_->data[0], size);
    av_frame_->data[2] = std::next(av_frame_->data[1], size / 4);
    av_frame_->pts = Time::Now();

    if (const auto ret = avcodec_send_frame(codec_context_, av_frame_); ret < 0) {
      LOGE << "Error encoding frame in avcodec_send_frame.";
      return {};
    }
    if (const auto ret = avcodec_receive_packet(codec_context_, packet_); ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      return {};
    } else if (ret < 0) {
      LOGE << "Error encoding frame in avcodec_receive_packet.";
      return {};
    }

    auto result = Packet{packet_->data, static_cast<uint32_t>(packet_->size)};
    av_packet_unref(packet_);
    return std::move(result);
  }
};

}  // namespace eillance
