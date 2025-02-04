#include <jni.h>

#include <string>
#include <cassert>
#include <vector>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>

#include "hela/eillance/logging/logging.h"

namespace {

/// @brief Global JVM object
JavaVM* javaVm = nullptr;

}  // namespace

ANativeWindow* nativeWindow = nullptr;
AMediaCodec* mediaCodec = nullptr;
std::vector<uint8_t> sps, pps;
bool codecConfigured = false;

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject surface) {
  nativeWindow = ANativeWindow_fromSurface(env, surface);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_releaseDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv*, jobject) {
  if (mediaCodec != nullptr) {
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (nativeWindow != nullptr) {
    ANativeWindow_release(nativeWindow);
    nativeWindow = nullptr;
  }
}

void ParseSps(const uint8_t* data, size_t size, std::int32_t* width, std::int32_t* height) {
  // Simplified: Replace with actual SPS parsing
  *width = 1280;
  *height = 720;
}

void ConfigureCodec(const uint8_t* spsData, size_t spsSize, const uint8_t* ppsData, size_t ppsSize) {
  mediaCodec = AMediaCodec_createDecoderByType("video/avc");
  AMediaFormat* format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");

  std::int32_t width{0};
  std::int32_t height{0};
  ParseSps(spsData, spsSize, &width, &height);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
  AMediaFormat_setBuffer(format, "csd-0", spsData, spsSize);
  AMediaFormat_setBuffer(format, "csd-1", ppsData, ppsSize);

  AMediaCodec_configure(mediaCodec, format, nativeWindow, nullptr, 0);
  AMediaCodec_start(mediaCodec);
  AMediaFormat_delete(format);
  codecConfigured = true;
}

void ProcessFrame(std::vector<uint8_t> data) {
  std::size_t start_code_len = 0;
  if (data.size() >= 4 && memcmp(data.data(), "\x00\x00\x00\x01", 4) == 0) {
    start_code_len = 4;
  } else if (data.size() >= 3 && memcmp(data.data(), "\x00\x00\x01", 3) == 0) {
    start_code_len = 3;
  } else {
    return;
  }

  std::uint8_t nal_type = data[start_code_len] & 0x1F;
  if (nal_type == 7) { // SPS
    sps.assign(data.begin() + start_code_len, data.end());
  } else if (nal_type == 8) { // PPS
    pps.assign(data.begin() + start_code_len, data.end());
  }

  if (!sps.empty() && !pps.empty() && !codecConfigured) {
    ConfigureCodec(sps.data(), sps.size(), pps.data(), pps.size());
  }

  if (codecConfigured) {
    auto nal_size = data.size() - start_code_len;
    uint8_t header[4] = {
        (uint8_t)(nal_size >> 24),
        (uint8_t)(nal_size >> 16),
        (uint8_t)(nal_size >> 8),
        (uint8_t)nal_size
    };

    auto idx = AMediaCodec_dequeueInputBuffer(mediaCodec, 10000);
    if (idx >= 0) {
      std::size_t buf_size;
      uint8_t* buf = AMediaCodec_getInputBuffer(mediaCodec, idx, &buf_size);
      if ((buf != nullptr) && (nal_size + 4) <= buf_size) {
        memcpy(buf, header, 4);
        memcpy(buf + 4, data.data() + start_code_len, nal_size);
        AMediaCodec_queueInputBuffer(mediaCodec, idx, 0, nal_size + 4, 0, 0);
      }
    }

    AMediaCodecBufferInfo info;
    auto out_idx = AMediaCodec_dequeueOutputBuffer(mediaCodec, &info, 0);
    if (out_idx >= 0) {
      AMediaCodec_releaseOutputBuffer(mediaCodec, out_idx, true);
    }
  }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  javaVm = vm;
  return JNI_VERSION_1_6;
}
