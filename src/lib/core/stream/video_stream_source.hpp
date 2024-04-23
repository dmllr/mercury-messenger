#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
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

#include "core/rtc/stream.hpp"
#include "rtc/common.hpp"

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

  void start() override {
    #ifdef __ANDROID__
    constexpr auto cv_capabilities = cv::CAP_ANDROID;
    #else
    constexpr auto cv_capabilities = cv::CAP_ANY;
    #endif

    // open the default camera
    cap.open(0, cv_capabilities);

    if (!cap.isOpened()) {
      std::cout << "Can't open the capture device." << std::endl;
      return;
    }
    cap >> cv_frame;
    if (cv_frame.empty()) {
      std::cerr << "Unexpected end of stream." << std::endl;
      return;
    }

    #ifdef __ANDROID__
    constexpr auto pixel_format = AV_PIX_FMT_NV12;
    #else
    constexpr auto pixel_format = AV_PIX_FMT_YUV420P;
    #endif

    // find h.264 codec and allocate the codec context
    const auto* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
      throw std::runtime_error("Codec not found");
    }
    LOGD << "Codec selected: [" << codec->id << "] " << codec->name << " | " << codec->long_name;
    const auto* pix_fmt = codec->pix_fmts;
    while (*pix_fmt != AV_PIX_FMT_NONE) {
      if (*pix_fmt == AV_PIX_FMT_YUV420P) {
        break;
      }
      pix_fmt++;
    }
    if (*pix_fmt == AV_PIX_FMT_NONE) {
      throw std::runtime_error("Unsupported pixel format");
    }

    codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
      throw std::runtime_error("Could not allocate codec context");
    }

    //codec_context->bit_rate = 400000; // todo nado?
    codec_context->width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    codec_context->height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    codec_context->time_base = AVRational{1, 25};
    codec_context->framerate = AVRational{25, 1};
    codec_context->gop_size = 10;
    codec_context->max_b_frames = 0;
    codec_context->pix_fmt = pixel_format;
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
      std::cerr << "Failed to allocate converted frame" << std::endl;
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

  void loadNextSample() override {
    cap >> cv_frame;
    if (cv_frame.empty()) {
      std::cerr << "Unexpected end of stream." << std::endl;
      return;
    }

    #ifdef __ANDROID__
    cv::cvtColor(cv_frame, cv_frame, cv::COLOR_GRAY2RGB);
    cv::cvtColor(cv_frame, cv_frame, cv::COLOR_RGB2YUV_I420);
    #else
    cv::cvtColor(cv_frame, cv_frame, cv::COLOR_BGR2YUV_I420);
    #endif

    av_frame->data[0] = cv_frame.data;
    av_frame->data[1] = cv_frame.data + codec_context->width * codec_context->height;
    av_frame->data[2] = cv_frame.data + codec_context->width * codec_context->height * 5 / 4;
    av_frame->pts = av_gettime_relative() / 1000;

    int ret = avcodec_send_frame(codec_context, av_frame);
    while (ret >= 0) {
      ret = avcodec_receive_packet(codec_context, packet);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      }
      if (ret < 0) {
        std::cerr << "Error encoding frame." << std::endl;
        return;
      }

      encoded_data.insert(
          encoded_data.end(),
          reinterpret_cast<std::byte*>(packet->data),
          reinterpret_cast<std::byte*>(packet->data + packet->size));
    }
  }

  auto getSample() -> rtc::binary override {
    auto sample = encoded_data;
    encoded_data.clear();

    LOGV << "Sending sample of size " << sample.size() << "b ";
    return sample;
  }

  void stop() override {
    av_frame_free(&av_frame);
    avcodec_free_context(&codec_context);
    cap.release();  // release the camera
  }

  auto getSampleTime_us() -> uint64_t override {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
  }

  auto getSampleDuration_us() -> uint64_t override {
    return 1000000000 * codec_context->framerate.num / codec_context->framerate.den;
  }
};
