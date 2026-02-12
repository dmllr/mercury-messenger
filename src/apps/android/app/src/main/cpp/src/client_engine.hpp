#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <jni.h>
#include <sys/system_properties.h>

#include "data_transport_impl_libwebrtc.hpp"
#include "eillance/capture/video_config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "media_transport_impl_libwebrtc.hpp"
#include "utils/class_refs.hpp"

namespace eillance {

class ClientEngine {
  std::shared_ptr<Client> client_;
  std::shared_ptr<view_model::ClientEventRouter> router_;
  JavaVM* jvm_;
  jobject service_ref_;

 public:
  ClientEngine(JavaVM* jvm, JNIEnv* env, jobject context) {
    jvm_ = jvm;
    service_ref_ = env->NewGlobalRef(context);

    auto& class_refs = ClassRefs::Instance();
    jmethodID get_files_dir_method = class_refs.Class("android/content/Context").GetMethod("getFilesDir");
    jobject file_object = env->CallObjectMethod(context, get_files_dir_method);

    jmethodID get_path_method = class_refs.Class("java/io/File").GetMethod("getPath");
    auto* path_jstring = (jstring)env->CallObjectMethod(file_object, get_path_method);
    char const* path_chars = env->GetStringUTFChars(path_jstring, nullptr);
    std::filesystem::path const db_path = std::filesystem::path(path_chars) / "mercury.db";
    env->ReleaseStringUTFChars(path_jstring, path_chars);

    auto storage = std::make_shared<storage::PersistentStorage>(std::make_unique<storage::SQLiteStorage>(db_path));

    auto data_transport = std::make_unique<transport::AndroidDataTransport>(jvm, context);
    auto media_transport = std::make_unique<transport::AndroidMediaTransport>(jvm, context);

    // Pass separate transports to Client
    client_ = std::make_shared<Client>(
        storage,
        std::make_shared<transport::DataTransport>(std::move(data_transport)),
        std::make_shared<transport::MediaTransport>(std::move(media_transport)),
        [this]() {
          jni::ScopedJniEnv env_scope(jvm_);
          if (auto* env = env_scope.Get()) {
            auto& cls = ClassRefs::Instance().Class("hela/eillance/CallService");
            jmethodID method = cls.GetMethod("onNativeCoreCrash");
            env->CallVoidMethod(service_ref_, method);
          }
        }
    );

    router_ = std::make_shared<view_model::ClientEventRouter>(client_);

    std::string device_name;
    auto stored_name = storage->GetSetting("device_name");
    if (stored_name) {
      device_name = *stored_name;
    }

    if (device_name.empty()) {
      char model_string[PROP_VALUE_MAX + 1];
      __system_property_get("ro.product.model", model_string);
      device_name = std::string(model_string);
      storage->SetSetting("device_name", device_name);
    }
    client_->SetName(device_name);
  }

  ClientEngine(ClientEngine&&) = delete;
  ClientEngine(ClientEngine const&) = delete;
  auto operator=(ClientEngine&&) -> ClientEngine& = delete;
  auto operator=(ClientEngine const&) -> ClientEngine& = delete;
  ~ClientEngine() {
    if (service_ref_) {
      jni::ScopedJniEnv env_scope(jvm_);
      if (auto* env = env_scope.Get()) env->DeleteGlobalRef(service_ref_);
    }
    Terminate();
  }

  [[nodiscard]] auto GetClient() const -> std::shared_ptr<Client> {
    return client_;
  }
  [[nodiscard]] auto GetRouter() const -> std::shared_ptr<view_model::ClientEventRouter> {
    return router_;
  }

  void Start() {
    client_->Start();
    LGI << "Client engine is initialized and ready.";
  }

  void Terminate() {
    client_->Terminate();
  }

  void ConfigureVideoCapture(VideoConfig const& config) const {
    Config core_config;
    core_config.video_config = config;
    client_->Configure(core_config);
  }
};

}  // namespace eillance
