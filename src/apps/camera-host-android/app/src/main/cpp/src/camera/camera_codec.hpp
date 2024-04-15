#pragma once

#include "media/NdkMediaCodec.h"
#include <media/NdkMediaFormat.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/native_window_jni.h>

#include "logging/logging.h"

class CameraCodec {

public:
  enum CodecStatus {
    UNKNOWN = 0,
    STARTED,
    STOPPED
  };

private:
  AMediaCodec* codec = nullptr;
  ANativeWindow* surface = nullptr;
  CodecStatus status = UNKNOWN;

public:
  AMediaCodec* getCodec() {
    return codec;
  }

  ANativeWindow* getSurface() {
    return surface;
  }

  CameraCodec() {
    codec = AMediaCodec_createEncoderByType("video/avc");
    Log::assert_or_die(static_cast<bool>(codec), "There is no 'video/avc' codec support available");
  }

  void start() {
    if (status == STARTED)
      return;

    configure();
    auto rc = AMediaCodec_start(codec);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_start failed");
    status = STARTED;
  }

  void stop() {
    if (status == STOPPED)
      return;

    auto rc = AMediaCodec_stop(codec);
    Log::assert_soft(AMEDIA_OK == rc, "AMediaCodec_stop failed");
    status = STOPPED;
  }

  void request(bool to_start) {
    if (to_start)
      start();
    else
      stop();
  }

private:
  void configure() {
    auto format = AMediaFormat_new();

    auto width = 1280;
    auto height = 720;
    auto frameRate = 30;
    //		auto bitRate = 57600;
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
    auto profile = 0x00010000;  // AVCProfileConstrainedBaseline, https://developer.android.com/reference/android/media/MediaCodecInfo.CodecProfileLevel#AVCProfileConstrainedBaseline
    auto level = 0x100;         // AVCLevel3, https://developer.android.com/reference/android/media/MediaCodecInfo.CodecProfileLevel
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_PROFILE, profile);
    AMediaFormat_setInt32(params, AMEDIAFORMAT_KEY_LEVEL, level);
    rc = AMediaCodec_setParameters(codec, params);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_setParameters failed");
    AMediaFormat_delete(params);

    rc = AMediaCodec_createInputSurface(codec, &surface);
    Log::assert_or_die(AMEDIA_OK == rc, "AMediaCodec_createInputSurface failed");
  }
};
