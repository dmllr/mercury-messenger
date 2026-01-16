#pragma once

#include <sys/system_properties.h>

#include <atomic>
#include <cstddef>
#include <jni.h>
#include <memory>
#include <string>
#include <vector>

#include "class_refs.hpp"
#include "eillance/capture/video_config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "transport/transport_impl_libwebrtc.hpp"

namespace eillance {

class ClientEngine {
  std::shared_ptr<Client> client_;
  std::shared_ptr<view_model::ClientEventRouter> router_;

public:
  ClientEngine(JavaVM* jvm, JNIEnv* env, jobject context) {
    auto& class_refs = ClassRefs::Instance();
    // Get the app's private files directory for the database
    jmethodID get_files_dir_method = class_refs.Class("android/content/Context").GetMethod("getFilesDir");
    jobject file_object = env->CallObjectMethod(context, get_files_dir_method);

    jmethodID get_path_method = class_refs.Class("java/io/File").GetMethod("getPath");
    auto path_jstring = (jstring)env->CallObjectMethod(file_object, get_path_method);
    const char* path_chars = env->GetStringUTFChars(path_jstring, nullptr);
    std::filesystem::path db_path = std::filesystem::path(path_chars) / "mercury.db";
    env->ReleaseStringUTFChars(path_jstring, path_chars);

    auto storage = std::make_shared<storage::PersistentStorage>(std::make_unique<storage::SQLiteStorage>(db_path));

    auto rtc_transport_impl = std::make_unique<transport::AndroidRtcTransport>(jvm, context);
    auto transport = std::make_shared<transport::Transport>(std::move(rtc_transport_impl));
    client_ = std::make_shared<Client>(storage, transport);
    router_ = std::make_shared<view_model::ClientEventRouter>(client_);

    std::string device_name;
    auto stored_name = storage->GetSetting("device_name");
    if (stored_name) { device_name = *stored_name; }

    if (device_name.empty()) {
      char model_string[PROP_VALUE_MAX + 1];
      __system_property_get("ro.product.model", model_string);
      device_name = std::string(model_string);
      storage->SetSetting("device_name", device_name);
    }
    client_->SetName(device_name);
  }

  ClientEngine(ClientEngine&&) = delete;
  ClientEngine(const ClientEngine&) = delete;
  auto operator=(ClientEngine&&) -> ClientEngine& = delete;
  auto operator=(const ClientEngine&) -> ClientEngine& = delete;
  ~ClientEngine() { Terminate(); }

  [[nodiscard]] auto GetClient() const -> std::shared_ptr<Client> { return client_; }
  [[nodiscard]] auto GetRouter() const -> std::shared_ptr<view_model::ClientEventRouter> { return router_; }

  void Start() {
    client_->Start();
    LOGI << "Client engine is initialized and ready.";
  }

  void Terminate() { client_->Terminate(); }

  void ConfigureVideoCapture(const VideoConfig& config) const {
    Config core_config;
    core_config.video_config = config;
    client_->Configure(core_config);
  }
};

}  // namespace eillance
