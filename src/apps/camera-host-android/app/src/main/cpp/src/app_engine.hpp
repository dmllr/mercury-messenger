#pragma once

#include <functional>
#include <thread>

#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "hela/eillance/events/event_source.h"
#include "hela/eillance/rtc/producer.hpp"
#include "hela/eillance/utils/string.hpp"


namespace hela::eillance {

class AppEngine {
  JavaVM* jvm_;
  JNIEnv* env_;
  jobject service_instance_;
  jobject status_update_callback_;
  std::shared_ptr<hela::eillance::RtcTransport<hela::eillance::RtcProducer>> rtc_producer_;

public:
  explicit AppEngine(JavaVM* jvm, JNIEnv* env, jobject instance, jobject status_update_callback)
      : jvm_(jvm),
        env_(env),
        service_instance_(env->NewWeakGlobalRef(instance)),
        status_update_callback_(env->NewGlobalRef(status_update_callback)) {
    EventQueue::Instance();
    rtc_producer_ = std::make_shared<hela::eillance::RtcTransport<hela::eillance::RtcProducer>>();
    rtc_producer_->SetName(GetSharedPreferenceValue("device_name", StringUtils::RandomString()));
    rtc_producer_->SetStatusCallback([this](EventSource sender, int status) { SetStatus(sender, status); });
    DispatchQueue::MainThread().dispatch([this]() {
      rtc_producer_->Start();
    });
  }

  ~AppEngine() {
    env_->DeleteGlobalRef(service_instance_);
    env_->DeleteGlobalRef(status_update_callback_);
  }

  Status GetStatus() {
    return rtc_producer_->GetStatus();
  }

  void ConfigureDeviceName(const std::string& device_name) {
    rtc_producer_->SetName(device_name);
  }

  std::string GetSharedPreferenceValue(const std::string& key, const std::string& default_value) {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return default_value;  // Handle the case where env is not attached
    }

    // Retrieve the SharedPreferences object from the service_instance_
    jclass service_class = env->GetObjectClass(service_instance_);
    jmethodID get_shared_pref_method = env->GetMethodID(service_class, "getSharedPreferences", "(Ljava/lang/String;I)Landroid/content/SharedPreferences;");

    if (get_shared_pref_method == nullptr) {
      return default_value;
    }

    // Name of SharedPreferences and mode (0 for MODE_PRIVATE)
    jstring pref_name = env->NewStringUTF("your_shared_pref_name");
    jobject shared_preferences = env->CallObjectMethod(service_instance_, get_shared_pref_method, pref_name, 0);
    env->DeleteLocalRef(pref_name);

    if (shared_preferences == nullptr) {
      return default_value;
    }

    // Get the 'getString' method from SharedPreferences class
    jclass shared_pref_class = env->GetObjectClass(shared_preferences);
    jmethodID get_string_method = env->GetMethodID(shared_pref_class, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

    if (get_string_method == nullptr) {
      return default_value;
    }

    // Convert the key and default_value to jstring
    jstring j_key = env->NewStringUTF(key.c_str());
    jstring j_default_value = env->NewStringUTF(default_value.c_str());

    // Call getString on the SharedPreferences object
    jstring j_result = (jstring)env->CallObjectMethod(shared_preferences, get_string_method, j_key, j_default_value);

    // Release local references
    env->DeleteLocalRef(j_key);
    env->DeleteLocalRef(j_default_value);

    if (j_result == nullptr) {
      return default_value;  // Return default if not found
    }

    // Convert jstring to std::string
    const char* result_cstr = env->GetStringUTFChars(j_result, nullptr);
    std::string result(result_cstr);
    env->ReleaseStringUTFChars(j_result, result_cstr);

    return result;
  }

private:
  JNIEnv* AttachCurrentThreadIfNeeded() {
    JNIEnv* env;
    jint res = jvm_->GetEnv((void**)&env, JNI_VERSION_1_6);
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

  void SetStatus(EventSource sender, int status) {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return;
    }
    jclass callback_class = env->GetObjectClass(status_update_callback_);
    jmethodID callback_method = env->GetMethodID(callback_class, "onStatusUpdate", "(II)V");
    if (callback_method != nullptr) {
      env->CallVoidMethod(status_update_callback_, callback_method, static_cast<int>(sender), status);
    }
  }

};

}  // namespace hela::eillance
