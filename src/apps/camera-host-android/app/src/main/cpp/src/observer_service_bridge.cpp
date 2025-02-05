#include <jni.h>

#include <string>
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>

#include <rtc/rtc.hpp>

#include "hela/eillance/rtc/consumer.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/string.hpp"
#include "hela/eillance/utils/h264.hpp"

ANativeWindow* nativeWindow = nullptr;
AMediaCodec* mediaCodec = nullptr;
bool codecConfigured = false;
std::int32_t currentWidth = 0, currentHeight = 0;
hela::eillance::RtcConsumer consumer{};
H264Parser h264Parser;

/// @brief Initializes the codec with SPS/PPS.
void ConfigureCodec(const std::vector<std::byte>& sps, const std::vector<std::byte>& pps) {
  if (codecConfigured) {
    LOGI << "Codec already configured, skipping reconfiguration.";
    return;
  }

  // Convert std::vector<std::byte> to std::vector<uint8_t>
  std::vector<uint8_t> spsBytes(sps.size());
  std::vector<uint8_t> ppsBytes(pps.size());

  std::memcpy(spsBytes.data(), sps.data(), sps.size());
  std::memcpy(ppsBytes.data(), pps.data(), pps.size());

  AMediaFormat* format = AMediaFormat_new();
  if (!format) {
    LOGE << "Failed to create AMediaFormat.";
    return;
  }

  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
  AMediaFormat_setBuffer(format, "csd-0", spsBytes.data(), spsBytes.size());
  AMediaFormat_setBuffer(format, "csd-1", ppsBytes.data(), ppsBytes.size());
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, currentWidth);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, currentHeight);

  mediaCodec = AMediaCodec_createDecoderByType("video/avc");
  if (!mediaCodec) {
    LOGE << "Failed to create AMediaCodec.";
    AMediaFormat_delete(format);
    return;
  }

  if (AMediaCodec_configure(mediaCodec, format, nativeWindow, nullptr, 0) != AMEDIA_OK) {
    LOGE << "AMediaCodec configuration failed.";
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  } else {
    AMediaCodec_start(mediaCodec);
    codecConfigured = true;
    LOGI << "MediaCodec successfully configured.";
  }

  AMediaFormat_delete(format);
}

/// @brief Sends the parsed frame data to the codec.
bool HandleInputBuffer(const std::vector<std::byte>& frameBuffer) {
  if (!codecConfigured) {
    LOGW << "Codec not configured yet, dropping frame.";
    return false;
  }

  ssize_t bufIndex = AMediaCodec_dequeueInputBuffer(mediaCodec, 0);
  if (bufIndex < 0) {
    LOGW << "No available input buffer.";
    return false;
  }

  size_t bufSize;
  uint8_t* buf = AMediaCodec_getInputBuffer(mediaCodec, bufIndex, &bufSize);
  if (!buf) {
    LOGW << "Failed to get input buffer.";
    return false;
  }

  size_t copySize = std::min(frameBuffer.size(), bufSize);
  std::memcpy(buf, frameBuffer.data(), copySize);
  AMediaCodec_queueInputBuffer(mediaCodec, bufIndex, 0, copySize, 0, 0);

  return true;
}

/// @brief Initializes the H.264 parser and sets up callbacks.
void InitH264Parser() {
  h264Parser.SetOnConfigureCallback([](const std::vector<std::byte>& sps, const std::vector<std::byte>& pps) {
    LOGI << "Received SPS/PPS, configuring codec...";
    ConfigureCodec(sps, pps);
  });

  h264Parser.SetOnInputBufferCallback([](const std::vector<std::byte>& frameBuffer) {
    return HandleInputBuffer(frameBuffer);
  });

  h264Parser.SetOnOutputBufferCallback([]() {
    LOGI << "Output frame available (callback).";
  });
}

/// @brief JNI function to initialize the decoder with a SurfaceTexture from Kotlin.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initDecoder(JNIEnv* env, jobject, jobject surface) {
  if (nativeWindow) {
    ANativeWindow_release(nativeWindow);
  }
  nativeWindow = ANativeWindow_fromSurface(env, surface);
  if (!nativeWindow) {
    LOGE << "Failed to obtain ANativeWindow from Surface.";
    return;
  }

  InitH264Parser();

  consumer.SetName("CLI::" + std::string(StringUtils::RandomString()));
  consumer.Start();
  consumer.SetClientAddedCallback([]() {
    consumer.SendStreamRequest();
  });
  consumer.SetFrameReceivedCallback([](const rtc::binary& frame) {
    h264Parser.ProcessFrame(frame);
  });

  LOGI << "Decoder initialized and ready.";
}

/// @brief JNI function to release decoder resources.
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_releaseDecoder(JNIEnv*, jobject) {
  consumer.Stop();
  if (mediaCodec) {
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (nativeWindow) {
    ANativeWindow_release(nativeWindow);
    nativeWindow = nullptr;
  }
  codecConfigured = false;
  LOGI << "Decoder resources released.";
}
