#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/mat.hpp>

extern "C" {
#include "libavcodec/codec.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/packet.h"
#include "libavutil/error.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixfmt.h"
#include "libavutil/time.h"
#include "libavutil/frame.h"
}

#include "core/cross-platform/properties.hpp"
#include "core/rtc/stream.hpp"
#include "rtc/common.hpp"
#include "utils/units.hpp"

#ifdef _WIN32
  #include <combaseapi.h>
#endif

class VideoStreamSource : public Stream::Source {
private:
  cv::VideoCapture cap;
  cv::Mat cv_frame;
  rtc::binary encoded_data;
  std::chrono::time_point<std::chrono::steady_clock> start_time;
  AVCodecContext* codec_context = nullptr;

  AVFrame* av_frame = nullptr;
  AVPacket* packet = nullptr;

public:
  VideoStreamSource() = default;

  void Start() override {
    // open the default camera
    cap.open(0, CrossPlatformProperties::OpenCVCameraCapabilities);

    if (!cap.isOpened()) {
      LOGE << "Can't open the capture device. This source has not been started.";
      return;
    }
    cap >> cv_frame;
    if (cv_frame.empty()) {
      LOGE << "Can't capture a test frame. This source has not been started.";
      return;
    }

    // find h.264 codec and allocate the codec context
    const auto* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
      throw std::runtime_error("Codec not found");
    }
    LOGD << "Codec selected: [" << codec->id << "] " << codec->name << " | " << codec->long_name;

    codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
      throw std::runtime_error("Could not allocate codec context");
    }

    codec_context->width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    codec_context->height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    codec_context->time_base = AVRational{1, 25};
    codec_context->framerate = AVRational{25, 1};
    codec_context->gop_size = 10;
    codec_context->max_b_frames = 0;
    codec_context->pix_fmt = CrossPlatformProperties::AVCodecDestPixelFormat;
    av_opt_set(codec_context->priv_data, "startcode", "1", 0);
    if (codec->id == AV_CODEC_ID_H264) {
      av_opt_set(codec_context->priv_data, "preset", "slow", 0);
    }

    #ifdef _WIN32
    // TODO investigate
    // essential to set hw_encoding=0 on Windows due to NVidia MF failure:
    // [h264_mf @ 0000024BC438BD40] MFT name: 'NVIDIA H.264 Encoder MFT'
    // [h264_mf @ 0000024BC438BD40] "format negotiation failed"
    av_opt_set(codec_context->priv_data, "hw_encoding", "0", 0);
    #endif

    LOGV << "Width: " << codec_context->width << ", height: " << codec_context->height;
    LOGV << "Input pixel format: " << av_get_pix_fmt_name(codec_context->pix_fmt);
    LOGV << "Output pixel format: " << av_get_pix_fmt_name(codec_context->codec->pix_fmts[0]);

    #ifdef _WIN32
    CoUninitialize();
    #endif
    auto ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret < 0) {
      throw std::runtime_error("Could not open codec " + std::to_string(ret));
    }

    // initialize the frame
    // Allocate a frame to hold the converted image
    av_frame = av_frame_alloc();
    if (av_frame == nullptr) {
      LOGE << "Failed to allocate converted frame";
      return;
    }

    const auto num_bytes = av_image_get_buffer_size(codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
    std::vector<uint8_t> converted_frame_buffer(num_bytes);
    av_image_fill_arrays(av_frame->data, av_frame->linesize, converted_frame_buffer.data(), codec_context->pix_fmt, codec_context->width, codec_context->height, 1);
    av_frame->width = codec_context->width;
    av_frame->height = codec_context->height;
    av_frame->format = codec_context->pix_fmt;

    packet = av_packet_alloc();

    start_time = std::chrono::steady_clock::now();
  }

  void LoadNextSample() override {
    cap >> cv_frame;
    if (cv_frame.empty()) {
      LOGE << "Unexpected end of stream.";
      return;
    }

    #ifdef __ANDROID__
    cv::cvtColor(cv_frame, cv_frame, cv::COLOR_GRAY2RGB);
    #endif
    cv::cvtColor(cv_frame, cv_frame, cv::COLOR_BGR2YUV_I420);

    // set YUV pointers
    const auto size = codec_context->width * codec_context->height;
    av_frame->data[0] = cv_frame.data;
    av_frame->data[1] = std::next(av_frame->data[0], size);
    av_frame->data[2] = std::next(av_frame->data[1], size / 4);
    av_frame->pts = av_gettime_relative() / Units::Milliseconds;

    int ret = avcodec_send_frame(codec_context, av_frame);
    while (ret >= 0) {
      ret = avcodec_receive_packet(codec_context, packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      }
      if (ret < 0) {
        LOGE << "Error encoding frame.";
        return;
      }

      encoded_data.insert(
          encoded_data.end(),
          reinterpret_cast<std::byte*>(packet->data),
          reinterpret_cast<std::byte*>(std::next(packet->data, packet->size)));
    }
  }

  auto GetSample() -> rtc::binary override {
    auto sample = encoded_data;
    encoded_data.clear();

    LOGV << "Sending sample of size " << sample.size() << "b ";
    return sample;
  }

  void Stop() override {
    av_frame_free(&av_frame);
    avcodec_free_context(&codec_context);
    cap.release();
  }

  auto GetSampleTime_us() -> uint64_t override {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
  }

};
