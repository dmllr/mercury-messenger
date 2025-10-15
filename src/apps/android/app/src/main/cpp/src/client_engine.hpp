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

#include "hela/eillance/common/event_queue.hpp"
#include "hela/eillance/capture/video_config.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/rtc/client.hpp"
#include "hela/eillance/utils/h264.hpp"
#include "hela/eillance/utils/string.hpp"

#include "class_refs.hpp"
#include "shared_preferences.hpp"

namespace hela::eillance {

class ClientEngine {
  JavaVM* jvm_;
  jobject native_bridge_{nullptr};
  SharedPreferences prefs_;

  ANativeWindow* native_window_{nullptr};
  AMediaCodec* media_codec_{nullptr};
  std::atomic<bool> is_codec_configured_{false};

  RtcClient rtc_client_{};
  H264Parser h264_parser_{};

  DispatchQueue codec_dispatch_queue_{"Codec", 1};

public:
  ClientEngine(JavaVM* jvm, JNIEnv* env, jobject instance)
      : jvm_(jvm),
        native_bridge_(env->NewGlobalRef(instance)),
        prefs_(jvm, native_bridge_) {
    EventQueue::Instance();

    auto device_id = prefs_.GetString("device_id", "");
    if (device_id.empty()) {
      device_id = StringUtils::RandomString();
      prefs_.SetString("device_id", device_id);
    }
    auto device_name = prefs_.GetString("device_name", "");
    if (device_name.empty()) {
      char model_string[PROP_VALUE_MAX + 1];
      __system_property_get("ro.product.model", model_string);
      device_name = std::string(model_string);
      prefs_.SetString("device_name", device_name);
    }
    rtc_client_.SetId(device_id);
    rtc_client_.SetName(device_name);
    rtc_client_.SetStatusCallback(
        [this](const std::string& peer_id, EventSource sender, int status) {
          SetStatus(peer_id, sender, status);
        });
  }

  ClientEngine(ClientEngine&&) = delete;
  ClientEngine(const ClientEngine&) = delete;
  auto& operator=(ClientEngine&&) = delete;
  auto& operator=(const ClientEngine&) = delete;

  ~ClientEngine() {
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
    if (native_window_ != nullptr) {
      ANativeWindow_release(native_window_);
    }
    native_window_ = ANativeWindow_fromSurface(env, surface);
  }

  void Start() {
    rtc_client_.Start();
    rtc_client_.SetPeerListUpdateCallback([this](const std::vector<Device>& peers) {
      UpdatePeerList(peers);
    });
    rtc_client_.SetProfileReceivedCallback([this](const std::string& peer_id, const Device::Profile& profile) {
      UpdateProfile(peer_id, profile);
    });
    rtc_client_.SetFrameReceivedCallback([this](const rtc::binary& frame) {
      h264_parser_.ProcessFrame(frame);
      if (codec_dispatch_queue_.Size() > 10) {
        LOGW << "Decoding queue is full, dropping frames.";
        codec_dispatch_queue_.RemovePending();
      }
      codec_dispatch_queue_.Dispatch([this, frame]() {
        HandleInputBuffer(frame);
      });
    });
    LOGI << "Client engine is initialized and ready.";
  }

  void Stop() {
    is_codec_configured_ = false;
    if (media_codec_ != nullptr) {
      AMediaCodec_stop(media_codec_);
      AMediaCodec_delete(media_codec_);
      media_codec_ = nullptr;
    }
    if (native_window_ != nullptr) {
      ANativeWindow_release(native_window_);
      native_window_ = nullptr;
    }
    LOGI << "Decoder resources released.";
  }

  void Terminate() {
    Stop();
    rtc_client_.Terminate();
  }

  void RequestPeerListUpdate() {
    rtc_client_.RequestPeerList();
  }

  void Call(const std::string& peer_id) {
    InitH264Parser();
    rtc_client_.Call(peer_id);
  }

  void ConfigureVideoCapture(const VideoConfig& config) {
    Config coreConfig;
    coreConfig.video_config = config;
    rtc_client_.Configure(coreConfig);
  }

private:
  JNIEnv* AttachCurrentThreadIfNeeded() {
    JNIEnv* env{};
    const jint res = jvm_->GetEnv((void**)&env, JNI_VERSION_1_6);
    switch (res) {
      case JNI_OK:
        return env;
      case JNI_EDETACHED:
        if (jvm_->AttachCurrentThread(&env, nullptr) == JNI_OK) {
          return env;
        } else {
          return nullptr;
        }
      default:
        return nullptr;
    }
  }

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

    LOGI << "Configuring decoder with dimensions: " << conf.width << "x" << conf.height;

    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, conf.width);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, conf.height);

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

  void HandleInputBuffer(const std::vector<std::byte>& frameBuffer) {
    if (!is_codec_configured_) {
      LOGW << "Codec not configured yet, dropping frame.";
      return;
    }

    constexpr auto timeout = 5000ULL;
    auto buf_index = AMediaCodec_dequeueInputBuffer(media_codec_, timeout);
    if (buf_index >= 0) {
      size_t buf_size = 0;
      auto *buf = AMediaCodec_getInputBuffer(media_codec_, buf_index, &buf_size);
      if (buf != nullptr) {
        const auto copy_size = std::min(frameBuffer.size(), buf_size);
        std::memcpy(buf, frameBuffer.data(), copy_size);
        AMediaCodec_queueInputBuffer(media_codec_, buf_index, 0, copy_size, 0, 0);
      }
    }

    AMediaCodecBufferInfo info;
    auto output_buffer_index = AMediaCodec_dequeueOutputBuffer(media_codec_, &info, 0);
    if (output_buffer_index >= 0) {
      AMediaCodec_releaseOutputBuffer(media_codec_, output_buffer_index, info.size > 0);
    }
  }

  void InitH264Parser() {
    h264_parser_.Reset();
    h264_parser_.SetOnConfigureCallback([this](const VideoConfig conf) {
      LOGI << "Configuration change callback triggered, configuring codec...";
      ConfigureCodec(conf);
      ConfigureSurfaceResolution(conf);
    });
  }

  void ConfigureSurfaceResolution(VideoConfig config) {
    if (native_bridge_ == nullptr) return;
    auto* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) return;

    jclass callback_class = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_class, "onStreamInfoChange", "(III)V");
    if (callback_method != nullptr) {
      env->CallVoidMethod(native_bridge_, callback_method,
                          static_cast<int>(config.width),
                          static_cast<int>(config.height),
                          static_cast<int>(config.rotation));
    }
  }

  static void SetBridgeProfile(JNIEnv* env, jobject& java_list, const Device::Profile& profile) {
    auto& classref = ClassRefs::Instance();
    jmethodID add_method = env->GetMethodID(classref.Class("java/util/ArrayList").Class(), "add", "(Ljava/lang/Object;)Z");

    for (const auto& camera : profile) {
      jstring cam_id = env->NewStringUTF(camera.id.c_str());
      jstring cam_name = env->NewStringUTF(camera.name.c_str());
      jobject res_list = classref.Class("java/util/ArrayList").NewObject(env);

      for (const auto& res : camera.resolutions) {
        jobject jres = classref.Class("hela/eillance/ndkio/Resolution").NewObject(env, res.width, res.height);
        env->CallBooleanMethod(res_list, add_method, jres);
        env->DeleteLocalRef(jres);
      }

      jobject jcamera = classref.Class("hela/eillance/ndkio/Camera").NewObject(env, cam_id, cam_name, res_list);
      env->CallBooleanMethod(java_list, add_method, jcamera);

      env->DeleteLocalRef(jcamera);
      env->DeleteLocalRef(cam_id);
      env->DeleteLocalRef(cam_name);
      env->DeleteLocalRef(res_list);
    }
  }

  void UpdatePeerList(const std::vector<Device>& peers) {
    if (native_bridge_ == nullptr) return;
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) return;

    auto& classref = ClassRefs::Instance();
    jclass callback_cls = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_cls, "onPeerListUpdate", "(Ljava/util/List;)V");
    if (callback_method == nullptr) return;

    jobject java_list = classref.Class("java/util/ArrayList").NewObject(env);
    jmethodID add_method = env->GetMethodID(classref.Class("java/util/ArrayList").Class(), "add", "(Ljava/lang/Object;)Z");

    for (const auto& peer : peers) {
      jstring id = env->NewStringUTF(peer.id.c_str());
      jstring name = env->NewStringUTF(peer.name.c_str());
      jobject camera_list = classref.Class("java/util/ArrayList").NewObject(env);
      SetBridgeProfile(env, camera_list, peer.profile);

      jobject jpeer = classref.Class("hela/eillance/ndkio/Peer").NewObject(env, id, name, camera_list);
      env->CallBooleanMethod(java_list, add_method, jpeer);

      env->DeleteLocalRef(jpeer);
      env->DeleteLocalRef(id);
      env->DeleteLocalRef(name);
      env->DeleteLocalRef(camera_list);
    }

    env->CallVoidMethod(native_bridge_, callback_method, java_list);
    env->DeleteLocalRef(java_list);
  }

  void UpdateProfile(const std::string& peer_id, const Device::Profile& profile) {
    if (native_bridge_ == nullptr) return;
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) return;

    auto& classref = ClassRefs::Instance();
    jclass callback_cls = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_cls, "onDeviceProfileReceived", "(Ljava/lang/String;Ljava/util/List;)V");
    if (callback_method == nullptr) return;

    jstring jpeer_id = env->NewStringUTF(peer_id.c_str());
    jobject camera_list = classref.Class("java/util/ArrayList").NewObject(env);
    SetBridgeProfile(env, camera_list, profile);

    env->CallVoidMethod(native_bridge_, callback_method, jpeer_id, camera_list);

    env->DeleteLocalRef(jpeer_id);
    env->DeleteLocalRef(camera_list);
  }

  void SetStatus(const std::string& peer_id, EventSource sender, int status) {
    auto* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) return;

    jclass callback_class = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_class, "onStatusUpdate", "(Ljava/lang/String;II)V");
    if (callback_method != nullptr) {
      jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
      env->CallVoidMethod(native_bridge_, callback_method, j_peer_id, static_cast<int>(sender), status);
      env->DeleteLocalRef(j_peer_id);
    }
  }
};
}
