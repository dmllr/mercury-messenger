#pragma once

#include <jni.h>
#include <sys/system_properties.h>

#include <functional>
#include <thread>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "hela/eillance/events/event_source.h"
#include "hela/eillance/rtc/streamer.hpp"
#include "hela/eillance/utils/string.hpp"

#include "shared_preferences.hpp"

namespace hela::eillance {

class StreamerEngine {
  JavaVM* jvm_;
  jobject native_bridge_;
  SharedPreferences prefs_;
  RtcStreamer rtc_streamer_;

public:
  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  StreamerEngine(JavaVM* jvm, JNIEnv* env, jobject instance)
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
    rtc_streamer_.SetId(device_id);
    rtc_streamer_.SetName(device_name);
    rtc_streamer_.SetStatusCallback(
        [this](const std::string& peer_id, EventSource sender, int status) {
          SetStatus(peer_id, sender, status);
        });
    DispatchQueue::MainThread().Dispatch([this]() {
      rtc_streamer_.Start();
    });
  }

  StreamerEngine(StreamerEngine&&) = delete;
  StreamerEngine(const StreamerEngine&) = delete;
  StreamerEngine& operator=(StreamerEngine&&) = delete;
  StreamerEngine& operator=(const StreamerEngine&) = delete;

  ~StreamerEngine() {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      LOGE << "Failed to get JNIEnv in destructor.";
      return;
    }

    rtc_streamer_.SetStatusCallback(nullptr);

    if (native_bridge_ != nullptr && !env->IsSameObject(native_bridge_, nullptr)) {
      LOGD << "Deleting native_bridge_ GlobalRef.";
      env->DeleteGlobalRef(native_bridge_);
      native_bridge_ = nullptr;
    }
  }

  void Start() {
    DispatchQueue::MainThread().Dispatch([this]() {
      rtc_streamer_.Start();
    });
  }

  void Stop() {
    DispatchQueue::MainThread().Dispatch([this]() {
      rtc_streamer_.Terminate();
    });
  }

  void UpdateAuthToken(const std::string& token) {
    rtc_streamer_.UpdateAuthToken(token);
  }

  ServiceConnectionState GetServiceConnectionState() {
    return rtc_streamer_.GetServiceConnectionState();
  }

  void ConfigureDeviceName(const std::string& device_name) {
    rtc_streamer_.SetName(device_name);
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
