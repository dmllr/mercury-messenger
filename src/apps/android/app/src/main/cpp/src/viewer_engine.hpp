#pragma once

#include <jni.h>
#include <sys/system_properties.h>

#include <atomic>
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
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/viewer.hpp"
#include "hela/eillance/utils/h264.hpp"
#include "hela/eillance/utils/string.hpp"

#include "class_refs.hpp"

namespace hela::eillance {

class ViewerEngine {
  JavaVM* jvm_;
  jobject native_bridge_{nullptr};

  ANativeWindow* native_window_{nullptr};
  AMediaCodec* media_codec_{nullptr};
  std::atomic<bool> is_codec_configured_{false};

  RtcViewer rtc_viewer_{};
  H264Parser h264_parser_{};

  hela::eillance::EventQueue::SubscriberTypeId configure_subscriber_id_{0};

public:
  ViewerEngine(JavaVM* jvm, JNIEnv*)
      : jvm_(jvm) {
    EventQueue::Instance();

//    auto device_id = GetSharedPreferenceValue("device_id", "");
//    if (device_id.empty()) {
//      device_id = StringUtils::RandomString();
//      SetSharedPreferenceValue("device_id", device_id);
//    }
//    auto device_name = GetSharedPreferenceValue("device_name", "");
//    if (device_name.empty()) {
//      char model_string[PROP_VALUE_MAX + 1];
//      __system_property_get("ro.product.model", model_string);
//      device_name = std::string(model_string);
//      SetSharedPreferenceValue("device_name", device_name);
//    }
//    rtc_viewer_.SetId(device_id);
//    rtc_viewer_.SetName(device_name);
    rtc_viewer_.SetId(StringUtils::RandomString());
    char model_string[PROP_VALUE_MAX + 1];
    __system_property_get("ro.product.model", model_string);
    rtc_viewer_.SetName(std::string(model_string));

    rtc_viewer_.SetStatusCallback([this](const std::string& peer_id, EventSource sender, int status) { SetStatus(peer_id, sender, status); });
  }
  ViewerEngine(ViewerEngine&&) = delete;
  ViewerEngine(const ViewerEngine&) = delete;
  auto& operator=(ViewerEngine&&) = delete;
  auto& operator=(const ViewerEngine&) = delete;

  ~ViewerEngine() {
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
    native_window_ = ANativeWindow_fromSurface(env, surface);
  }

  void SetNativeBridge(JNIEnv* env, jobject native_bridge) {
    native_bridge_ = env->NewGlobalRef(native_bridge);
  }

  void Start() {
    rtc_viewer_.Start();
    rtc_viewer_.SetClientChangedCallback([this](const std::vector<Device>& clients) {
      UpdateClientList(clients);
    });
    rtc_viewer_.SetProfileReceivedCallback([this](const std::string& peer_id, const Device::Profile& profile) {
      UpdateProfile(peer_id, profile);
    });
    rtc_viewer_.SetFrameReceivedCallback([this](const rtc::binary& frame) {
      h264_parser_.ProcessFrame(frame);
    });
    configure_subscriber_id_ = EventQueue::Instance().Subscribe<VideoConfig>(
        [this](VideoConfig config) { ConfigureSurfaceResolution(config); });
    LOGI << "Viewer engine is initialized and ready.";
  }

  void StopStreaming() {
    EventQueue::Instance().Unsubscribe<VideoConfig>(configure_subscriber_id_);
    rtc_viewer_.StopStreaming();
    if (media_codec_ != nullptr) {
      AMediaCodec_stop(media_codec_);
      AMediaCodec_delete(media_codec_);
      media_codec_ = nullptr;
    }
    if (native_window_ != nullptr) {
      ANativeWindow_release(native_window_);
      native_window_ = nullptr;
    }
    is_codec_configured_ = false;
    LOGI << "Decoder resources released.";
  }

  void Terminate() {
    StopStreaming();
    rtc_viewer_.Terminate();
  }

  void SendStreamRequest(const std::string& client_id, const Config& config) {
    InitH264Parser();
    rtc_viewer_.SendStreamRequest(client_id, config);
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
    if (is_codec_configured_) {
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

    media_codec_ = AMediaCodec_createDecoderByType("video/avc");
    if (media_codec_ == nullptr) {
      LOGE << "Failed to create AMediaCodec.";
      AMediaFormat_delete(format);
      return;
    }

    if (AMediaCodec_configure(media_codec_, format, native_window_, nullptr, 0) != AMEDIA_OK) {
      LOGE << "AMediaCodec configuration failed.";
      AMediaCodec_delete(media_codec_);
      media_codec_ = nullptr;
    } else {
      AMediaCodec_start(media_codec_);
      is_codec_configured_ = true;
      LOGI << "MediaCodec successfully configured.";
    }

    AMediaFormat_delete(format);
  }

  /// @brief Handles output buffers and renders frames to native_window_.
  void HandleOutputBuffer() {
    AMediaCodecBufferInfo info;
    while (true) {
      if ((!is_codec_configured_) || (media_codec_ == nullptr)) {
        return;
      }

      const auto output_buffer_index = AMediaCodec_dequeueOutputBuffer(media_codec_, &info, 0);

      if (output_buffer_index >= 0) {
        // Render the buffer to native_window_
        AMediaCodec_releaseOutputBuffer(media_codec_, output_buffer_index, true);
      } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        LOGI << "Output buffers changed.";
      } else if (output_buffer_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* new_format = AMediaCodec_getOutputFormat(media_codec_);
        LOGI << "Output format changed: " << AMediaFormat_toString(new_format);
        AMediaFormat_delete(new_format);
      } else if (output_buffer_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        // No available output buffer, exit the loop
        break;
      }
    }
  }

  /// @brief Sends the parsed frame data to the codec.
  void HandleInputBuffer(const std::vector<std::byte>& frameBuffer) {
    if (!is_codec_configured_) {
      LOGW << "Codec not configured yet, dropping frame.";
      return;
    }

    constexpr auto timeout = 10000ULL;
    auto buf_index = AMediaCodec_dequeueInputBuffer(media_codec_, timeout);
    if (buf_index < 0) {
      LOGW << "No available input buffer.";
      HandleOutputBuffer();
      return;
    }

    size_t buf_size = 0;
    auto *buf = AMediaCodec_getInputBuffer(media_codec_, buf_index, &buf_size);
    if (buf == nullptr) {
      LOGW << "Failed to get input buffer.";
      return;
    }

    const auto copy_size = std::min(frameBuffer.size(), buf_size);
    std::memcpy(buf, frameBuffer.data(), copy_size);
    AMediaCodec_queueInputBuffer(media_codec_, buf_index, 0, copy_size, 0, 0);

    // Process output buffers immediately after input
    HandleOutputBuffer();
  }

  /// @brief Initializes the H.264 parser and sets up callbacks.
  void InitH264Parser() {
    h264_parser_.Reset();
    h264_parser_.SetOnConfigureCallback([this](const VideoConfig conf) {
      LOGI << "Configuration change callback triggered, configuring codec...";
      ConfigureCodec(conf);
      EventQueue::Instance().Push(conf);
    });

    h264_parser_.SetOnInputBufferCallback([this](const std::vector<std::byte>& frameBuffer) {
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

  static void SetBridgeProfile(JNIEnv* env, jobject& java_list, const Device::Profile& profile) {
    auto& classref = ClassRefs::Instance();

    for (const auto& camera : profile) {
      jstring cam_id = env->NewStringUTF(camera.id.c_str());
      jstring cam_name = env->NewStringUTF(camera.name.c_str());

      jobject res_list = classref.Class("java/util/ArrayList").NewObject(env);
      jmethodID add_method = env->GetMethodID(classref.Class("java/util/ArrayList").Class(), "add", "(Ljava/lang/Object;)Z");

      for (const auto& res : camera.resolutions) {
        jobject jres = classref.Class("hela/eillance/ndkio/Resolution").NewObject(env, res.width, res.height);
        // NOLINTNEXTLINE(*-vararg)
        env->CallBooleanMethod(res_list, add_method, jres);
        env->DeleteLocalRef(jres);
      }

      jobject jcamera = classref.Class("hela/eillance/ndkio/Camera").NewObject(env, cam_id, cam_name, res_list);
      // NOLINTNEXTLINE(*-vararg)
      env->CallBooleanMethod(java_list, add_method, jcamera);

      env->DeleteLocalRef(jcamera);
      env->DeleteLocalRef(cam_id);
      env->DeleteLocalRef(cam_name);
      env->DeleteLocalRef(res_list);
    }
  }

  void UpdateClientList(const std::vector<Device>& clients) {
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

    jobject java_list = classref.Class("java/util/ArrayList").NewObject(env);
    jmethodID add_method = env->GetMethodID(classref.Class("java/util/ArrayList").Class(), "add", "(Ljava/lang/Object;)Z");

    for (const auto& client : clients) {
      jstring id = env->NewStringUTF(client.id.c_str());
      jstring name = env->NewStringUTF(client.name.c_str());
      jobject camera_list = classref.Class("java/util/ArrayList").NewObject(env);

      SetBridgeProfile(env, camera_list, client.profile);

      jobject jclient = classref.Class("hela/eillance/ndkio/Device").NewObject(env, id, name, camera_list);
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

  void UpdateProfile(const std::string& peer_id, const Device::Profile& profile) {
    if (native_bridge_ == nullptr) {
      return;
    }

    JNIEnv* env = nullptr;
    jvm_->AttachCurrentThread(&env, nullptr);
    if (env == nullptr) {
      return;
    }

    auto& classref = ClassRefs::Instance();
    jclass callback_cls = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_cls, "onDeviceProfileReceived", "(Ljava/lang/String;Ljava/util/List;)V");
    if (callback_method == nullptr) {
      env->DeleteLocalRef(callback_cls);
      return;
    }

    jstring jpeer_id = env->NewStringUTF(peer_id.c_str());
    jobject camera_list = classref.Class("java/util/ArrayList").NewObject(env);
    SetBridgeProfile(env, camera_list, profile);

    // NOLINTNEXTLINE(*-vararg)
    env->CallVoidMethod(native_bridge_, callback_method, jpeer_id, camera_list);

    env->DeleteLocalRef(jpeer_id);
    env->DeleteLocalRef(camera_list);
    env->DeleteLocalRef(callback_cls);
  }

  void SetStatus(const std::string& peer_id, EventSource sender, int status) {
    auto* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return;
    }

    jclass callback_class = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_class, "onStatusUpdate", "(Ljava/lang/String;II)V");
    if (callback_method != nullptr) {
      jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
      // NOLINTNEXTLINE(*-vararg)
      env->CallVoidMethod(native_bridge_, callback_method, j_peer_id, static_cast<int>(sender), status);
      env->DeleteLocalRef(j_peer_id);
    }
  }

};

}  // namespace hela::eillance
