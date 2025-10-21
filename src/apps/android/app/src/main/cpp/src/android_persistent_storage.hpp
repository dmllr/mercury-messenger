#pragma once

#include <jni.h>
#include <string>
#include <vector>
#include <optional>
#include <cstddef>

#include "eillance/storage/persistent_storage.hpp"
#include "eillance/crypto/crypto.hpp"
#include "class_refs.hpp"

#include <cassert>

namespace eillance::storage {

// Android SharedPreferences-based implementation of the storage interface.
class AndroidPersistentStorageImpl final : public PersistentStorageImpl {
    JavaVM* jvm_;
    jobject context_{nullptr};

    auto AttachCurrentThreadIfNeeded() -> JNIEnv* {
      JNIEnv* env{};
      const jint res = jvm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
      switch (res) {
        case JNI_OK:
          return env;
        case JNI_EDETACHED:
          if (jvm_->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            return env;
          }
          return nullptr;
        default:
          return nullptr;
      }
    }

public:
    AndroidPersistentStorageImpl(JavaVM* jvm, jobject context) : jvm_(jvm) {
        if(jvm == nullptr || context == nullptr) return;
        JNIEnv* env = AttachCurrentThreadIfNeeded();
        if (env) {
            context_ = env->NewGlobalRef(context);
        }
    }

    ~AndroidPersistentStorageImpl() override {
        if (context_) {
            JNIEnv* env = AttachCurrentThreadIfNeeded();
            if (env) {
                env->DeleteGlobalRef(context_);
            }
        }
    }

    auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool override {
        const std::string hex_value = crypto::ToHex(value);
        JNIEnv* env = AttachCurrentThreadIfNeeded();
        if (env == nullptr) return false;

        jclass preference_manager_class = ClassRefs::Instance().Class("androidx/preference/PreferenceManager").Class();
        jmethodID get_default_shared_pref_method = env->GetStaticMethodID(preference_manager_class, "getDefaultSharedPreferences", "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
        if (get_default_shared_pref_method == nullptr) return false;

        jobject shared_preferences = env->CallStaticObjectMethod(preference_manager_class, get_default_shared_pref_method, context_);
        if (shared_preferences == nullptr) return false;

        jclass shared_pref_class = env->GetObjectClass(shared_preferences);
        jmethodID edit_method = env->GetMethodID(shared_pref_class, "edit", "()Landroid/content/SharedPreferences$Editor;");
        jobject editor = env->CallObjectMethod(shared_preferences, edit_method);

        bool result = false;
        if (editor != nullptr) {
            jclass editor_class = env->GetObjectClass(editor);
            jmethodID put_string_method = env->GetMethodID(editor_class, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");
            jstring j_key = env->NewStringUTF(key.c_str());
            jstring j_value = env->NewStringUTF(hex_value.c_str());
            env->CallObjectMethod(editor, put_string_method, j_key, j_value);

            jmethodID commit_method = env->GetMethodID(editor_class, "commit", "()Z");
            assert(commit_method != nullptr);
            jboolean success = env->CallBooleanMethod(editor, commit_method);
            result = (success == JNI_TRUE);

            env->DeleteLocalRef(j_key);
            env->DeleteLocalRef(j_value);
            env->DeleteLocalRef(editor);
        }
        env->DeleteLocalRef(shared_preferences);
        return result;
    }

    auto Load(const std::string& key) -> std::optional<std::vector<std::byte>> override {
        JNIEnv* env = AttachCurrentThreadIfNeeded();
        if (env == nullptr) return std::nullopt;

        jclass preference_manager_class = ClassRefs::Instance().Class("androidx/preference/PreferenceManager").Class();
        jmethodID get_default_shared_pref_method = env->GetStaticMethodID(preference_manager_class, "getDefaultSharedPreferences", "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
        if (get_default_shared_pref_method == nullptr) return std::nullopt;

        jobject shared_preferences = env->CallStaticObjectMethod(preference_manager_class, get_default_shared_pref_method, context_);
        if (shared_preferences == nullptr) return std::nullopt;

        jclass shared_pref_class = env->GetObjectClass(shared_preferences);
        jmethodID get_string_method = env->GetMethodID(shared_pref_class, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        if (get_string_method == nullptr) {
            env->DeleteLocalRef(shared_preferences);
            return std::nullopt;
        }

        jstring j_key = env->NewStringUTF(key.c_str());
        auto* j_result = (jstring)env->CallObjectMethod(shared_preferences, get_string_method, j_key, nullptr);
        env->DeleteLocalRef(j_key);

        std::optional<std::vector<std::byte>> result = std::nullopt;
        if (j_result != nullptr) {
            const char* result_cstr = env->GetStringUTFChars(j_result, nullptr);
            std::string hex_value(result_cstr);
            env->ReleaseStringUTFChars(j_result, result_cstr);
            env->DeleteLocalRef(j_result);
            result = crypto::FromHex(hex_value);
        }

        env->DeleteLocalRef(shared_preferences);
        return result;
    }

    auto Exists(const std::string& key) -> bool override {
        JNIEnv* env = AttachCurrentThreadIfNeeded();
        if (env == nullptr) return false;

        jclass preference_manager_class = ClassRefs::Instance().Class("androidx/preference/PreferenceManager").Class();
        jmethodID get_default_shared_pref_method = env->GetStaticMethodID(preference_manager_class, "getDefaultSharedPreferences", "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
        if (get_default_shared_pref_method == nullptr) return false;

        jobject shared_preferences = env->CallStaticObjectMethod(preference_manager_class, get_default_shared_pref_method, context_);
        if (shared_preferences == nullptr) return false;

        jclass shared_pref_class = env->GetObjectClass(shared_preferences);
        jmethodID contains_method = env->GetMethodID(shared_pref_class, "contains", "(Ljava/lang/String;)Z");
        jstring j_key = env->NewStringUTF(key.c_str());
        jboolean has_key = env->CallBooleanMethod(shared_preferences, contains_method, j_key);
        env->DeleteLocalRef(j_key);
        env->DeleteLocalRef(shared_preferences);

        return has_key == JNI_TRUE;
    }

    auto Delete(const std::string& key) -> bool override {
        JNIEnv* env = AttachCurrentThreadIfNeeded();
        if (env == nullptr) return false;

        jclass preference_manager_class = ClassRefs::Instance().Class("androidx/preference/PreferenceManager").Class();
        jmethodID get_default_shared_pref_method = env->GetStaticMethodID(preference_manager_class, "getDefaultSharedPreferences", "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
        if (get_default_shared_pref_method == nullptr) return false;

        jobject shared_preferences = env->CallStaticObjectMethod(preference_manager_class, get_default_shared_pref_method, context_);
        if (shared_preferences == nullptr) return false;

        jclass shared_pref_class = env->GetObjectClass(shared_preferences);
        jmethodID edit_method = env->GetMethodID(shared_pref_class, "edit", "()Landroid/content/SharedPreferences$Editor;");
        jobject editor = env->CallObjectMethod(shared_preferences, edit_method);

        bool result = false;
        if (editor != nullptr) {
            jclass editor_class = env->GetObjectClass(editor);
            jmethodID remove_method = env->GetMethodID(editor_class, "remove", "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");
            jstring j_key = env->NewStringUTF(key.c_str());
            env->CallObjectMethod(editor, remove_method, j_key);

            jmethodID commit_method = env->GetMethodID(editor_class, "commit", "()Z");
            if (commit_method != nullptr) {
                jboolean success = env->CallBooleanMethod(editor, commit_method);
                result = (success == JNI_TRUE);
            }

            env->DeleteLocalRef(j_key);
            env->DeleteLocalRef(editor);
        }
        env->DeleteLocalRef(shared_preferences);
        return result;
    }
};

} // namespace eillance::storage
