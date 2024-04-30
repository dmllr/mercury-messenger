#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
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
#include "libavutil/time.h"
}
#ifdef _WIN32
  #include <combaseapi.h>
#endif

#include "hela/eillance/capture//video_config.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/units.hpp"

namespace hela::eillance {

class VideoEncoder {
  AVCodecContext* codec_context_ = nullptr;
  AVFrame* av_frame_ = nullptr;
  AVPacket* packet_ = nullptr;

  DataCallback data_callback_;

  static constexpr auto codecDestPixelFormat = AV_PIX_FMT_NV12;

public:
  void Engage(const int width, const int height, const int framerate) {
    // find h.264 codec and allocate the codec context
    const auto* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
      throw std::runtime_error("Codec not found");
    }
    LOGD << "Codec selected: [" << codec->id << "] " << codec->name << " | " << codec->long_name;

    codec_context_ = avcodec_alloc_context3(codec);
    if (codec_context_ == nullptr) {
      throw std::runtime_error("Could not allocate codec context");
    }

    codec_context_->width = static_cast<int>(width);
    codec_context_->height = static_cast<int>(height);
    codec_context_->time_base = AVRational{1, framerate};
    codec_context_->framerate = AVRational{framerate, 1};
    codec_context_->gop_size = 10;
    codec_context_->max_b_frames = 0;
    codec_context_->pix_fmt = codecDestPixelFormat;

    #ifdef _WIN32
    // TODO investigate, maybe fallback
    // essential to set hw_encoding=0 on Windows due to NVidia MF failure:
    // [h264_mf @ 0000024BC438BD40] MFT name: 'NVIDIA H.264 Encoder MFT'
    // [h264_mf @ 0000024BC438BD40] "format negotiation failed"
    av_opt_set(codec_context_->priv_data, "hw_encoding", "0", 0);
    #endif

    LOGD << "Width: " << codec_context_->width << ", height: " << codec_context_->height;
    LOGD << "Input pixel format: " << av_get_pix_fmt_name(codec_context_->pix_fmt);
    LOGD << "Output pixel format: " << av_get_pix_fmt_name( codec_context_->codec->pix_fmts[0]);

    #ifdef _WIN32
    CoUninitialize();
    #endif
    auto ret = avcodec_open2(codec_context_, codec, nullptr);
    if (ret < 0) {
      throw std::runtime_error("Could not open codec " + std::to_string(ret));
    }

    // initialize the frame
    // Allocate a frame to hold the converted image
    av_frame_ = av_frame_alloc();
    if (av_frame_ == nullptr) {
      LOGE << "Failed to allocate converted frame";
      return;
    }

    const auto num_bytes = av_image_get_buffer_size(codec_context_->pix_fmt, codec_context_->width, codec_context_->height, 1);
    std::vector<uint8_t> converted_frame_buffer(num_bytes);
    av_image_fill_arrays(av_frame_->data, av_frame_->linesize, converted_frame_buffer.data(), codec_context_->pix_fmt, codec_context_->width, codec_context_->height, 1);
    av_frame_->width = codec_context_->width;
    av_frame_->height = codec_context_->height;
    av_frame_->format = codec_context_->pix_fmt;

    packet_ = av_packet_alloc();
  }

  void Revoke() {
    av_frame_free(&av_frame_);
    av_packet_free(&packet_);
    avcodec_free_context(&codec_context_);
  }

  void EncodeBuffer(void* frames) {
    // set YUV pointers
    const auto size = codec_context_->width * codec_context_->height;
    av_frame_->data[0] = reinterpret_cast<std::uint8_t*>(frames);
    av_frame_->data[1] = std::next(av_frame_->data[0], size);
    av_frame_->data[2] = std::next(av_frame_->data[1], size / 4);
    av_frame_->pts = av_gettime_relative() / Units::Milliseconds;

    int ret = avcodec_send_frame(codec_context_, av_frame_);
    while (ret >= 0) {
      ret = avcodec_receive_packet(codec_context_, packet_);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      }
      if (ret < 0) {
        LOGE << "Error encoding frame.";
        return;
      }

      data_callback_(
          Packet{
              std::vector(
                  reinterpret_cast<std::byte*>(packet_->data),
                  reinterpret_cast<std::byte*>(std::next(packet_->data, packet_->size))),
              Time::Now()});

      av_packet_unref(packet_);
    }
  }

  void SetDataCallback(DataCallback callback) {
    data_callback_ = std::move(callback);
  }

};

}  // namespace hela::eillance
