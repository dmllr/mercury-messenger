#pragma once

#include <jni.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>

#include <rtc/rtc.hpp>

#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/rtc/consumer.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/string.hpp"
#include "hela/eillance/utils/h264.hpp"

#include "class_refs.hpp"

namespace hela::eillance {

class ObserverEngine {
  JavaVM* jvm_;
  jobject native_bridge_{nullptr};

  ANativeWindow* nativeWindow_{nullptr};
  AMediaCodec* mediaCodec_{nullptr};
  bool codecConfigured_{false};

  RtcConsumer consumer_{};
  H264Parser h264Parser_{};

  hela::eillance::EventQueue::SubscriberTypeId configure_subscriber_id_{0};

public:
  ObserverEngine(JavaVM* jvm, JNIEnv*)
      : jvm_(jvm) {
    EventQueue::Instance();
  }
  ObserverEngine(ObserverEngine&&) = delete;
  ObserverEngine(const ObserverEngine&) = delete;
  auto& operator=(ObserverEngine&&) = delete;
  auto& operator=(const ObserverEngine&) = delete;

  ~ObserverEngine() {
    Terminate();

    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      LOGE << "Failed to get JNIEnv in destructor.";
      return;
    }

    if (native_bridge_ != nullptr && (env->IsSameObject(native_bridge_, nullptr) == JNI_FALSE)) {
      LOGD << "Deleting native_bridge_ GlobalRef.";
      env->DeleteGlobalRef(native_bridge_);
      native_bridge_ = nullptr;
    }
  }

  void SetTargetSurface(JNIEnv* env, jobject surface) {
    nativeWindow_ = ANativeWindow_fromSurface(env, surface);
  }

  void SetNativeBridge(JNIEnv* env, jobject native_bridge) {
    native_bridge_ = env->NewGlobalRef(native_bridge);
  }

  void Start() {
    consumer_.SetName(std::string(StringUtils::RandomString()));
    consumer_.Start();
    consumer_.SetClientChangedCallback([this](const std::vector<ClientInfo>& clients) {
      UpdateClientList(clients);
    });
    consumer_.SetFrameReceivedCallback([this](const rtc::binary& frame) {
      h264Parser_.ProcessFrame(frame);
    });
    configure_subscriber_id_ = EventQueue::Instance().Subscribe<VideoConfig>(
        [this](VideoConfig config) { ConfigureSurfaceResolution(config); });
    LOGI << "Observer engine is initialized and ready.";
  }

  void StopStreaming() {
    EventQueue::Instance().Unsubscribe<VideoConfig>(configure_subscriber_id_);
    consumer_.StopStreaming();
    if (mediaCodec_ != nullptr) {
      AMediaCodec_stop(mediaCodec_);
      AMediaCodec_delete(mediaCodec_);
      mediaCodec_ = nullptr;
    }
    if (nativeWindow_ != nullptr) {
      ANativeWindow_release(nativeWindow_);
      nativeWindow_ = nullptr;
    }
    codecConfigured_ = false;
    LOGI << "Decoder resources released.";
  }

  void Terminate() {
    StopStreaming();
    consumer_.Terminate();
  }

  void SendStreamRequest(const std::string& client_id) {
    InitH264Parser();
    consumer_.SendStreamRequest(client_id);
  }

private:
  JNIEnv* AttachCurrentThreadIfNeeded() {
    JNIEnv* env{};
    const jint res = jvm_->GetEnv((void**)&env, JNI_VERSION_1_6);
    switch (res) {
      case JNI_OK:
        // the current thread is already attached, return the JNIEnv.
        return env;
      case JNI_EDETACHED:
        // the current thread is not attached, try to attach.
        if (jvm_->AttachCurrentThread(&env, nullptr) == JNI_OK) {
          return env;
        } else {
          return nullptr;
        }
      default:
        return nullptr;
    }
  }

  /// @brief Initializes the codec with @c VideoConfig.
  void ConfigureCodec(const VideoConfig& conf) {
    if (codecConfigured_) {
      LOGI << "Codec already configured, skipping reconfiguration.";
      return;
    }

    AMediaFormat* format = AMediaFormat_new();
    if (format == nullptr) {
      LOGE << "Failed to create AMediaFormat.";
      return;
    }

    const auto color_format = 0x7f000789;  // COLOR_FormatSurface
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, static_cast<std::int32_t>(conf.width));
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<std::int32_t>(conf.height));
    AMediaFormat_setInt32(format,AMEDIAFORMAT_KEY_COLOR_FORMAT, color_format);

    mediaCodec_ = AMediaCodec_createDecoderByType("video/avc");
    if (mediaCodec_ == nullptr) {
      LOGE << "Failed to create AMediaCodec.";
      AMediaFormat_delete(format);
      return;
    }

    if (AMediaCodec_configure(mediaCodec_, format, nativeWindow_, nullptr, 0) != AMEDIA_OK) {
      LOGE << "AMediaCodec configuration failed.";
      AMediaCodec_delete(mediaCodec_);
      mediaCodec_ = nullptr;
    } else {
      AMediaCodec_start(mediaCodec_);
      codecConfigured_ = true;
      LOGI << "MediaCodec successfully configured.";
    }

    AMediaFormat_delete(format);
  }

  /// @brief Handles output buffers and renders frames to nativeWindow_.
  void HandleOutputBuffer() {
    AMediaCodecBufferInfo info;
    while (true) {
      if (!codecConfigured_ || mediaCodec_ == nullptr) {
        return;
      }

      const auto output_buffer_index = AMediaCodec_dequeueOutputBuffer(mediaCodec_, &info, 0);

      if (output_buffer_index >= 0) {
        // Render the buffer to nativeWindow_
        AMediaCodec_releaseOutputBuffer(mediaCodec_, output_buffer_index, true);
      } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        LOGI << "Output buffers changed.";
      } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* newFormat = AMediaCodec_getOutputFormat(mediaCodec_);
        LOGI << "Output format changed: " << AMediaFormat_toString(newFormat);
        AMediaFormat_delete(newFormat);
      } else if (output_buffer_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        // No available output buffer, exit the loop
        break;
      }
    }
  }

  /// @brief Sends the parsed frame data to the codec.
  void HandleInputBuffer(const std::vector<std::byte>& frameBuffer) {
    if (!codecConfigured_) {
      LOGW << "Codec not configured yet, dropping frame.";
      return;
    }

    constexpr auto timeout = 10000ULL;
    auto buf_index = AMediaCodec_dequeueInputBuffer(mediaCodec_, timeout);
    if (buf_index < 0) {
      LOGW << "No available input buffer.";
      HandleOutputBuffer();
      return;
    }

    size_t buf_size = 0;
    auto *buf = AMediaCodec_getInputBuffer(mediaCodec_, buf_index, &buf_size);
    if (buf == nullptr) {
      LOGW << "Failed to get input buffer.";
      return;
    }

    const auto copy_size = std::min(frameBuffer.size(), buf_size);
    std::memcpy(buf, frameBuffer.data(), copy_size);
    AMediaCodec_queueInputBuffer(mediaCodec_, buf_index, 0, copy_size, 0, 0);

    // Process output buffers immediately after input
    HandleOutputBuffer();
  }

  /// @brief Initializes the H.264 parser and sets up callbacks.
  void InitH264Parser() {
    h264Parser_.Reset();
    h264Parser_.SetOnConfigureCallback([this](const VideoConfig conf) {
      LOGI << "Configuration change callback triggered, configuring codec...";
      ConfigureCodec(conf);
      EventQueue::Instance().Push(conf);
    });

    h264Parser_.SetOnInputBufferCallback([this](const std::vector<std::byte>& frameBuffer) {
      HandleInputBuffer(frameBuffer);
    });
  }

  void ConfigureSurfaceResolution(VideoConfig config) {
    if (native_bridge_ == nullptr) {
      return;
    }

    auto* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return;
    }

    jclass callback_class = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_class, "onStreamResolutionChange", "(II)V");
    if (callback_method != nullptr) {
      // NOLINTNEXTLINE(*-vararg)
      env->CallVoidMethod(native_bridge_, callback_method, static_cast<int>(config.width), static_cast<int>(config.height));
    }
  }

  void UpdateClientList(const std::vector<ClientInfo>& clients) {
    if (native_bridge_ == nullptr) {
      return;
    }

    JNIEnv* env = nullptr;
    jvm_->AttachCurrentThread(&env, nullptr);
    if (env == nullptr) {
      return;
    }

    auto& classref = ClassRefs::Instance();

    // Look up the callback method.
    jclass callback_cls = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_cls, "onClientListUpdate", "(Ljava/util/List;)V");
    if (callback_method == nullptr) {
      env->DeleteLocalRef(callback_cls);
      return;
    }

    // Create the Java ArrayList to hold ClientInfo objects.
    jobject java_list = classref.Class("java/util/ArrayList").NewObject(env);

    for (const auto& client : clients) {
      jstring id = env->NewStringUTF(client.id.c_str());
      jstring name = env->NewStringUTF(client.name.c_str());

      // Create Camera list
      jobject camera_list = classref.Class("java/util/ArrayList").NewObject(env);

      for (const auto& camera : client.profile) {
        jstring cam_id = env->NewStringUTF(camera.id.c_str());
        jstring cam_name = env->NewStringUTF(camera.name.c_str());

        // Create Resolution list
        jobject res_list = classref.Class("java/util/ArrayList").NewObject(env);

        for (const auto& res : camera.resolutions) {
          jobject jres = classref.Class("hela/eillance/ndkio/Resolution")
                             .NewObject(env, res.width, res.height);
          jmethodID add_method = env->GetMethodID(classref.Class("java/util/ArrayList").Class(), "add", "(Ljava/lang/Object;)Z");
          // NOLINTNEXTLINE(*-vararg)
          env->CallBooleanMethod(res_list, add_method, jres);

          env->DeleteLocalRef(jres);
        }

        jobject jcamera = classref.Class("hela/eillance/ndkio/Camera")
                              .NewObject(env, cam_id, cam_name, res_list);
        jmethodID add_method = env->GetMethodID(classref.Class("java/util/ArrayList").Class(), "add", "(Ljava/lang/Object;)Z");
        // NOLINTNEXTLINE(*-vararg)
        env->CallBooleanMethod(camera_list, add_method, jcamera);

        env->DeleteLocalRef(jcamera);
        env->DeleteLocalRef(cam_id);
        env->DeleteLocalRef(cam_name);
        env->DeleteLocalRef(res_list);
      }

      jobject jclient = classref.Class("hela/eillance/ndkio/ClientInfo")
                            .NewObject(env, id, name, camera_list);
      jmethodID add_method = env->GetMethodID(classref.Class("java/util/ArrayList").Class(), "add", "(Ljava/lang/Object;)Z");
      // NOLINTNEXTLINE(*-vararg)
      env->CallBooleanMethod(java_list, add_method, jclient);

      env->DeleteLocalRef(jclient);
      env->DeleteLocalRef(id);
      env->DeleteLocalRef(name);
      env->DeleteLocalRef(camera_list);
    }

    // NOLINTNEXTLINE(*-vararg)
    env->CallVoidMethod(native_bridge_, callback_method, java_list);

    env->DeleteLocalRef(java_list);
    env->DeleteLocalRef(callback_cls);
  }

};

}  // namespace hela::eillance
