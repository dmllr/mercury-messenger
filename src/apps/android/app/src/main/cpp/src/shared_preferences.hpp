#pragma once

#include <jni.h>
#include <string>

namespace hela::eillance {

  class SharedPreferences {
  private:
    JavaVM* jvm_;
    jobject context_;

  public:
    explicit SharedPreferences(JavaVM* jvm, jobject context)
        : jvm_(jvm), context_(context) {}

    bool HasValue(const std::string& key) {
      JNIEnv* env = AttachCurrentThreadIfNeeded();
      if (env == nullptr) {
        return false;
      }

      auto* preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
      auto* get_default_shared_pref_method = env->GetStaticMethodID(
          preference_manager_class,
          "getDefaultSharedPreferences",
          "(Landroid/content/Context;)Landroid/content/SharedPreferences;"
      );

      if (get_default_shared_pref_method == nullptr) {
        return false;
      }

      auto* shared_preferences = env->CallStaticObjectMethod(
          preference_manager_class,
          get_default_shared_pref_method,
          context_
      );

      if (shared_preferences == nullptr) {
        return false;
      }

      auto* shared_pref_class = env->GetObjectClass(shared_preferences);
      auto* contains_method = env->GetMethodID(shared_pref_class, "contains", "(Ljava/lang/String;)Z");

      if (contains_method == nullptr) {
        return false;
      }

      auto* j_key = env->NewStringUTF(key.c_str());
      const auto has_value = env->CallBooleanMethod(shared_preferences, contains_method, j_key);
      env->DeleteLocalRef(j_key);

      return has_value == JNI_TRUE;
    }

    std::string GetString(const std::string& key, const std::string& default_value) {
      JNIEnv* env = AttachCurrentThreadIfNeeded();
      if (env == nullptr) {
        return default_value;
      }

      jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
      jmethodID get_default_shared_pref_method = env->GetStaticMethodID(
          preference_manager_class,
          "getDefaultSharedPreferences",
          "(Landroid/content/Context;)Landroid/content/SharedPreferences;"
      );

      if (get_default_shared_pref_method == nullptr) {
        return default_value;
      }

      jobject shared_preferences = env->CallStaticObjectMethod(
          preference_manager_class,
          get_default_shared_pref_method,
          context_
      );

      if (shared_preferences == nullptr) {
        return default_value;
      }

      jclass shared_pref_class = env->GetObjectClass(shared_preferences);
      jmethodID get_string_method = env->GetMethodID(shared_pref_class, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

      if (get_string_method == nullptr) {
        return default_value;
      }

      jstring j_key = env->NewStringUTF(key.c_str());
      jstring j_default_value = env->NewStringUTF(default_value.c_str());

      auto* j_result = reinterpret_cast<jstring>(env->CallObjectMethod(shared_preferences, get_string_method, j_key, j_default_value));

      env->DeleteLocalRef(j_key);
      env->DeleteLocalRef(j_default_value);

      if (j_result == nullptr) {
        return default_value;
      }

      const char* result_cstr = env->GetStringUTFChars(j_result, nullptr);
      std::string result(result_cstr);
      env->ReleaseStringUTFChars(j_result, result_cstr);

      return result;
    }

    void SetString(const std::string& key, const std::string& value) {
      JNIEnv* env = AttachCurrentThreadIfNeeded();
      if (env == nullptr) {
        return;
      }

      jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
      jmethodID get_default_shared_pref_method = env->GetStaticMethodID(
          preference_manager_class,
          "getDefaultSharedPreferences",
          "(Landroid/content/Context;)Landroid/content/SharedPreferences;"
      );

      if (get_default_shared_pref_method == nullptr) {
        return;
      }

      jobject shared_preferences = env->CallStaticObjectMethod(
          preference_manager_class,
          get_default_shared_pref_method,
          context_
      );

      if (shared_preferences == nullptr) {
        return;
      }

      jclass shared_pref_class = env->GetObjectClass(shared_preferences);
      jmethodID edit_method = env->GetMethodID(shared_pref_class, "edit", "()Landroid/content/SharedPreferences$Editor;");
      jobject editor = env->CallObjectMethod(shared_preferences, edit_method);

      if (editor != nullptr) {
        jclass shared_pref_editor_class = env->FindClass("android/content/SharedPreferences$Editor");
        jmethodID put_string_method = env->GetMethodID(shared_pref_editor_class, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");

        jstring j_key = env->NewStringUTF(key.c_str());
        jstring j_value = env->NewStringUTF(value.c_str());

        env->CallObjectMethod(editor, put_string_method, j_key, j_value);

        jmethodID apply_method = env->GetMethodID(shared_pref_editor_class, "apply", "()V");
        env->CallVoidMethod(editor, apply_method);

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
  };

}  // namespace hela::eillance
