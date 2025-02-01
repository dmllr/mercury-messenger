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
  jobject service_instance_;
  jobject native_bridge_;
  std::shared_ptr<RtcProducer> rtc_producer_;

public:
  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  explicit AppEngine(JavaVM* jvm, JNIEnv* env, jobject instance, jobject native_bridge)
      : jvm_(jvm),
        service_instance_(env->NewWeakGlobalRef(instance)),
        native_bridge_(env->NewGlobalRef(native_bridge)) {
    EventQueue::Instance();

    auto device_name = GetSharedPreferenceValue("device_name", "");
    if (device_name.empty()) {
      device_name = StringUtils::RandomString();
      SetSharedPreferenceValue("device_name", device_name);
    }
    rtc_producer_ = std::make_shared<RtcProducer>();
    rtc_producer_->SetName(device_name);
    rtc_producer_->SetStatusCallback([this](const std::string& peer_id, EventSource sender, int status) { SetStatus(peer_id, sender, status); });
    DispatchQueue::MainThread().Dispatch([this]() {
      rtc_producer_->Start();
    });
  }

  AppEngine(AppEngine&&) = delete;
  AppEngine(const AppEngine&) = delete;
  AppEngine& operator=(AppEngine&&) = delete;
  AppEngine& operator=(const AppEngine&) = delete;

  ~AppEngine() {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      LOGE << "Failed to get JNIEnv in destructor.";
      return;
    }

    rtc_producer_->SetStatusCallback(nullptr);

    if (service_instance_ != nullptr && (env->IsSameObject(service_instance_, nullptr) == 0U)) {
      LOGD << "Deleting service_instance_ GlobalRef.";
      env->DeleteWeakGlobalRef(service_instance_);
      service_instance_ = nullptr;
    }

    if (native_bridge_ != nullptr && !env->IsSameObject(native_bridge_, nullptr)) {
      LOGD << "Deleting native_bridge_ GlobalRef.";
      env->DeleteGlobalRef(native_bridge_);
      native_bridge_ = nullptr;
    }
  }

  void Start() {
    DispatchQueue::MainThread().Dispatch([this]() {
      rtc_producer_->Start();
    });
  }

  void Stop() {
    DispatchQueue::MainThread().Dispatch([this]() {
      rtc_producer_->Stop();
    });
  }

  ServiceConnectionState GetServiceConnectionState() {
    return rtc_producer_->GetServiceConnectionState();
  }

  void ConfigureDeviceName(const std::string& device_name) {
    rtc_producer_->SetName(device_name);
  }

  bool HasSharedPreferenceValue(const std::string& key) {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return false;  // Handle the case where env is not attached
    }

    // Retrieve the SharedPreferences object via PreferenceManager.getDefaultSharedPreferences()
    auto* preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
    auto* get_default_shared_pref_method = env->GetStaticMethodID(
        preference_manager_class,
        "getDefaultSharedPreferences",
        "(Landroid/content/Context;)Landroid/content/SharedPreferences;"
    );

    if (get_default_shared_pref_method == nullptr) {
      return false;
    }

    // Call getDefaultSharedPreferences with the current context (service_instance_)
    // NOLINTNEXTLINE(*-vararg)
    auto* shared_preferences = env->CallStaticObjectMethod(
        preference_manager_class,
        get_default_shared_pref_method,
        service_instance_
    );

    if (shared_preferences == nullptr) {
      return false;
    }

    // Get the 'contains' method from SharedPreferences class
    auto* shared_pref_class = env->GetObjectClass(shared_preferences);
    auto* contains_method = env->GetMethodID(shared_pref_class, "contains", "(Ljava/lang/String;)Z");

    if (contains_method == nullptr) {
      return false;
    }

    // Convert the key to jstring
    auto* j_key = env->NewStringUTF(key.c_str());

    // Check if the key exists in the shared preferences
    // NOLINTNEXTLINE(*-vararg)
    const auto has_value = env->CallBooleanMethod(shared_preferences, contains_method, j_key);

    // Release local references
    env->DeleteLocalRef(j_key);

    return has_value == JNI_TRUE;  // Return true if key exists, false otherwise
  }

  std::string GetSharedPreferenceValue(const std::string& key, const std::string& default_value) {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return default_value;  // Handle the case where env is not attached
    }

    // Retrieve the SharedPreferences object via PreferenceManager.getDefaultSharedPreferences()
    jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
    jmethodID get_default_shared_pref_method = env->GetStaticMethodID(
        preference_manager_class,
        "getDefaultSharedPreferences",
        "(Landroid/content/Context;)Landroid/content/SharedPreferences;"
    );

    if (get_default_shared_pref_method == nullptr) {
      return default_value;
    }

    // Call getDefaultSharedPreferences with the current context (service_instance_)
    // NOLINTNEXTLINE(*-vararg)
    jobject shared_preferences = env->CallStaticObjectMethod(
        preference_manager_class,
        get_default_shared_pref_method,
        service_instance_
    );

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
    // NOLINTNEXTLINE(*-vararg)
    auto* j_result = reinterpret_cast<jstring>(env->CallObjectMethod(shared_preferences, get_string_method, j_key, j_default_value));

    // Release local references
    env->DeleteLocalRef(j_key);
    env->DeleteLocalRef(j_default_value);

    if (j_result == nullptr) {
      return default_value;  // Return default if not found
    }

    const char* result_cstr = env->GetStringUTFChars(j_result, nullptr);
    std::string result(result_cstr);
    env->ReleaseStringUTFChars(j_result, result_cstr);

    return result;
  }

  void SetSharedPreferenceValue(const std::string& key, const std::string& value) {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return;  // Handle the case where env is not attached
    }

    // Retrieve the SharedPreferences object via PreferenceManager.getDefaultSharedPreferences()
    jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
    jmethodID get_default_shared_pref_method = env->GetStaticMethodID(
        preference_manager_class,
        "getDefaultSharedPreferences",
        "(Landroid/content/Context;)Landroid/content/SharedPreferences;"
    );

    if (get_default_shared_pref_method == nullptr) {
      return;
    }

    // Call getDefaultSharedPreferences with the current context (service_instance_)
    // NOLINTNEXTLINE(*-vararg)
    jobject shared_preferences = env->CallStaticObjectMethod(
        preference_manager_class,
        get_default_shared_pref_method,
        service_instance_
    );

    if (shared_preferences == nullptr) {
      return;
    }

    // Get the 'edit' method from SharedPreferences class to start modifying the preferences
    jclass shared_pref_class = env->GetObjectClass(shared_preferences);
    jmethodID edit_method = env->GetMethodID(shared_pref_class, "edit", "()Landroid/content/SharedPreferences$Editor;");
    // NOLINTNEXTLINE(*-vararg)
    jobject editor = env->CallObjectMethod(shared_preferences, edit_method);

    if (editor != nullptr) {
      // Get the 'putString' method from SharedPreferences.Editor class
      jclass shared_pref_editor_class = env->FindClass("android/content/SharedPreferences$Editor");
      jmethodID put_string_method = env->GetMethodID(shared_pref_editor_class, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");

      // Convert key and value to jstring
      jstring j_key = env->NewStringUTF(key.c_str());
      jstring j_value = env->NewStringUTF(value.c_str());

      // Save the value using putString
      // NOLINTNEXTLINE(*-vararg)
      env->CallObjectMethod(editor, put_string_method, j_key, j_value);

      // Apply the changes
      jmethodID apply_method = env->GetMethodID(shared_pref_editor_class, "apply", "()V");
      // NOLINTNEXTLINE(*-vararg)
      env->CallVoidMethod(editor, apply_method);

      // Release local references
      env->DeleteLocalRef(j_key);
      env->DeleteLocalRef(j_value);
    }
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
