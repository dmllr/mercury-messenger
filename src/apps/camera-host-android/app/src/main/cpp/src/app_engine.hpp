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

    auto device_name = GetSharedPreferenceValue("device_name", "");
    if (device_name.empty()) {
      device_name = StringUtils::RandomString();
      SetSharedPreferenceValue("device_name", device_name);
    }
    rtc_producer_ = std::make_shared<hela::eillance::RtcTransport<hela::eillance::RtcProducer>>();
    rtc_producer_->SetName(device_name);
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

  bool HasSharedPreferenceValue(const std::string& key) {
    JNIEnv* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) {
      return false;  // Handle the case where env is not attached
    }

    // Retrieve the SharedPreferences object via PreferenceManager.getDefaultSharedPreferences()
    jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
    jmethodID get_default_shared_pref_method = env->GetStaticMethodID(
        preference_manager_class,
        "getDefaultSharedPreferences",
        "(Landroid/content/Context;)Landroid/content/SharedPreferences;"
    );

    if (get_default_shared_pref_method == nullptr) {
      return false;
    }

    // Call getDefaultSharedPreferences with the current context (service_instance_)
    jobject shared_preferences = env->CallStaticObjectMethod(
        preference_manager_class,
        get_default_shared_pref_method,
        service_instance_
    );

    if (shared_preferences == nullptr) {
      return false;
    }

    // Get the 'contains' method from SharedPreferences class
    jclass shared_pref_class = env->GetObjectClass(shared_preferences);
    jmethodID contains_method = env->GetMethodID(shared_pref_class, "contains", "(Ljava/lang/String;)Z");

    if (contains_method == nullptr) {
      return false;
    }

    // Convert the key to jstring
    jstring j_key = env->NewStringUTF(key.c_str());

    // Check if the key exists in the shared preferences
    jboolean has_value = env->CallBooleanMethod(shared_preferences, contains_method, j_key);

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
    auto* j_result = static_cast<jstring>(env->CallObjectMethod(shared_preferences, get_string_method, j_key, j_default_value));

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
    jobject editor = env->CallObjectMethod(shared_preferences, edit_method);

    if (editor != nullptr) {
      // Get the 'putString' method from SharedPreferences.Editor class
      jclass shared_pref_editor_class = env->FindClass("android/content/SharedPreferences$Editor");
      jmethodID put_string_method = env->GetMethodID(shared_pref_editor_class, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");

      // Convert key and value to jstring
      jstring j_key = env->NewStringUTF(key.c_str());
      jstring j_value = env->NewStringUTF(value.c_str());

      // Save the value using putString
      env->CallObjectMethod(editor, put_string_method, j_key, j_value);

      // Apply the changes
      jmethodID apply_method = env->GetMethodID(shared_pref_editor_class, "apply", "()V");
      env->CallVoidMethod(editor, apply_method);

      // Release local references
      env->DeleteLocalRef(j_key);
      env->DeleteLocalRef(j_value);
    }
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
