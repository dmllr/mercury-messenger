#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <filesystem>
#include <stdexcept>

#include "eillance/logging/logging.h"

#ifdef __ANDROID__
#include <jni.h>
#include "eillance/crypto/crypto.hpp"
#else
#include <fstream>
#endif

// Forward declarations for Android JNI types
struct _JNIEnv;
struct _JavaVM;
typedef _JavaVM JavaVM;
typedef struct _jobject* jobject;


namespace eillance::storage {

// A configuration variant for different platforms.
struct Config {
#ifdef __ANDROID__
  struct Android {
    JavaVM* jvm;
    jobject context;
  };
  std::variant<std::filesystem::path, Android> platform_config;
#else
  std::variant<std::filesystem::path> platform_config;
#endif
};

// The private Impl interface, defined here where it's a complete type.
// It serves as the base class for our platform-specific implementations.
class PersistentStorageImpl {
public:
  virtual ~PersistentStorageImpl() = default;
  virtual auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool = 0;
  virtual auto Load(const std::string& key) -> std::optional<std::vector<std::byte>> = 0;
  virtual auto Exists(const std::string& key) -> bool = 0;
  virtual auto Delete(const std::string& key) -> bool = 0;
};


class PersistentStorage {
public:
  explicit PersistentStorage(const Config& config) {
#ifdef __ANDROID__
    // The Android implementation is defined as a local class inside the constructor.
    class AndroidPersistentStorageImpl : public PersistentStorageImpl {
        JavaVM* jvm_;
        jobject context_;

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
        AndroidPersistentStorageImpl(JavaVM* jvm, jobject context) : jvm_(jvm), context_(context) {}

        auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool override {
            const std::string hex_value = crypto::ToHex(value);
            JNIEnv* env = AttachCurrentThreadIfNeeded();
            if (env == nullptr) return false;

            jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
            jmethodID get_default_shared_pref_method = env->GetStaticMethodID(preference_manager_class, "getDefaultSharedPreferences", "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
            if (get_default_shared_pref_method == nullptr) return false;

            jobject shared_preferences = env->CallStaticObjectMethod(preference_manager_class, get_default_shared_pref_method, context_);
            if (shared_preferences == nullptr) return false;

            jclass shared_pref_class = env->GetObjectClass(shared_preferences);
            jmethodID edit_method = env->GetMethodID(shared_pref_class, "edit", "()Landroid/content/SharedPreferences$Editor;");
            jobject editor = env->CallObjectMethod(shared_preferences, edit_method);

            if (editor != nullptr) {
                jclass editor_class = env->GetObjectClass(editor);
                jmethodID put_string_method = env->GetMethodID(editor_class, "putString", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");
                jstring j_key = env->NewStringUTF(key.c_str());
                jstring j_value = env->NewStringUTF(hex_value.c_str());
                env->CallObjectMethod(editor, put_string_method, j_key, j_value);

                jmethodID apply_method = env->GetMethodID(editor_class, "apply", "()V");
                env->CallVoidMethod(editor, apply_method);

                env->DeleteLocalRef(j_key);
                env->DeleteLocalRef(j_value);
                env->DeleteLocalRef(editor);
            }
            env->DeleteLocalRef(shared_preferences);
            return true;
        }

        auto Load(const std::string& key) -> std::optional<std::vector<std::byte>> override {
            JNIEnv* env = AttachCurrentThreadIfNeeded();
            if (env == nullptr) return std::nullopt;

            jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
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

            jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
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

            jclass preference_manager_class = env->FindClass("androidx/preference/PreferenceManager");
            jmethodID get_default_shared_pref_method = env->GetStaticMethodID(preference_manager_class, "getDefaultSharedPreferences", "(Landroid/content/Context;)Landroid/content/SharedPreferences;");
            if (get_default_shared_pref_method == nullptr) return false;

            jobject shared_preferences = env->CallStaticObjectMethod(preference_manager_class, get_default_shared_pref_method, context_);
            if (shared_preferences == nullptr) return false;

            jclass shared_pref_class = env->GetObjectClass(shared_preferences);
            jmethodID edit_method = env->GetMethodID(shared_pref_class, "edit", "()Landroid/content/SharedPreferences$Editor;");
            jobject editor = env->CallObjectMethod(shared_preferences, edit_method);

            if (editor != nullptr) {
                jclass editor_class = env->GetObjectClass(editor);
                jmethodID remove_method = env->GetMethodID(editor_class, "remove", "(Ljava/lang/String;)Landroid/content/SharedPreferences$Editor;");
                jstring j_key = env->NewStringUTF(key.c_str());
                env->CallObjectMethod(editor, remove_method, j_key);

                jmethodID apply_method = env->GetMethodID(editor_class, "apply", "()V");
                env->CallVoidMethod(editor, apply_method);

                env->DeleteLocalRef(j_key);
                env->DeleteLocalRef(editor);
            }
            env->DeleteLocalRef(shared_preferences);
            return true;
        }
    };

    if (std::holds_alternative<Config::Android>(config.platform_config)) {
        const auto& android_config = std::get<Config::Android>(config.platform_config);
        pimpl_ = std::make_unique<AndroidPersistentStorageImpl>(android_config.jvm, android_config.context);
    } else {
        throw std::runtime_error("Invalid config for Android platform");
    }
#else
    // The file-based implementation is defined as a local class inside the constructor.
    class FilePersistentStorageImpl : public PersistentStorageImpl {
        std::filesystem::path root_path_;
    public:
        explicit FilePersistentStorageImpl(std::filesystem::path root_path)
            : root_path_(std::move(root_path)) {
            if (!std::filesystem::exists(root_path_)) {
                std::filesystem::create_directories(root_path_);
            }
        }

        auto Save(const std::string& key, const std::vector<std::byte>& value) -> bool override {
            try {
                std::ofstream file(root_path_ / key, std::ios::binary | std::ios::trunc);
                if (!file.is_open()) {
                    LOGE << "Failed to open file for writing: " << (root_path_ / key).string();
                    return false;
                }
                file.write(reinterpret_cast<const char*>(value.data()), static_cast<std::streamsize>(value.size()));
                return true;
            } catch (const std::exception& e) {
                LOGE << "Exception while saving to file: " << e.what();
                return false;
            }
        }

        auto Load(const std::string& key) -> std::optional<std::vector<std::byte>> override {
            try {
                std::ifstream file(root_path_ / key, std::ios::binary | std::ios::ate);
                if (!file.is_open()) {
                    return std::nullopt;
                }
                const std::streamsize size = file.tellg();
                file.seekg(0, std::ios::beg);

                std::vector<std::byte> buffer(size);
                if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
                    return buffer;
                }
            } catch (const std::exception& e) {
                LOGE << "Exception while loading from file: " << e.what();
            }
            return std::nullopt;
        }

        auto Exists(const std::string& key) -> bool override {
            return std::filesystem::exists(root_path_ / key);
        }

        auto Delete(const std::string& key) -> bool override {
            try {
                return std::filesystem::remove(root_path_ / key);
            } catch (const std::filesystem::filesystem_error& e) {
                LOGE << "Error deleting file: " << e.what();
                return false;
            }
        }
    };

    if (std::holds_alternative<std::filesystem::path>(config.platform_config)) {
        pimpl_ = std::make_unique<FilePersistentStorageImpl>(std::get<std::filesystem::path>(config.platform_config));
    } else {
        throw std::runtime_error("Invalid config for this platform");
    }
#endif
  }
  ~PersistentStorage() = default;
  PersistentStorage(const PersistentStorage&) = delete;
  PersistentStorage& operator=(const PersistentStorage&) = delete;
  PersistentStorage(PersistentStorage&&) noexcept = default;
  PersistentStorage& operator=(PersistentStorage&&) noexcept = default;

  auto Save(const std::string& key, const std::vector<std::byte>& value) const -> bool {
    return pimpl_->Save(key, value);
  }
  auto Load(const std::string& key) const -> std::optional<std::vector<std::byte>> {
    return pimpl_->Load(key);
  }
  [[nodiscard]] auto Exists(const std::string& key) const -> bool {
    return pimpl_->Exists(key);
  }
  auto Delete(const std::string& key) const -> bool {
    return pimpl_->Delete(key);
  }

private:
  std::unique_ptr<PersistentStorageImpl> pimpl_;
};

}  // namespace eillance::storage
