#pragma once

#include <sys/system_properties.h>

#include <atomic>
#include <cstddef>
#include <jni.h>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "class_refs.hpp"
#include "eillance/capture/video_config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/logging/logging.h"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/view_model/mercury_view_model.hpp"
#include "transport/transport_impl_libwebrtc.hpp"


namespace eillance {

class ClientEngine {
  JavaVM* jvm_;
  jobject native_bridge_{nullptr};

  std::shared_ptr<Client> client_;
  std::unique_ptr<view_model::MercuryViewModel> view_model_;

public:
  ClientEngine(JavaVM* jvm, JNIEnv* env, jobject context, jobject native_bridge) : jvm_(jvm), native_bridge_(env->NewGlobalRef(native_bridge)) {
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

    view_model_ = std::make_unique<view_model::MercuryViewModel>(client_);

    view_model_->SetOnStateChangedCallback([this]() { UpdateState(); });

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

  auto ViewModel() -> view_model::MercuryViewModel* { return view_model_.get(); }

  void Start() {
    client_->Start();
    LOGI << "Client engine is initialized and ready.";
  }

  void Stop() {}

  void Terminate() {
    Stop();
    client_->Terminate();
  }

  void Call(const std::string& peer_id) const { client_->Call(peer_id); }

  void Hangup() const {
    if (view_model_) { view_model_->Hangup(); }
  }

  void ConfigureVideoCapture(const VideoConfig& config) const {
    Config core_config;
    core_config.video_config = config;
    client_->Configure(core_config);
  }

private:
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

  void UpdateState() {
    if (native_bridge_ == nullptr) { return; }
    auto* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) { return; }

    auto& callback_class_holder = ClassRefs::Instance().Class("hela/eillance/CallService");
    jmethodID callback_method = callback_class_holder.GetMethod("onStateUpdate");
    if (callback_method != nullptr) {
      nlohmann::json state_json = view_model_->GetState();
      std::string state_str = state_json.dump();
      jstring j_state_str = env->NewStringUTF(state_str.c_str());
      env->CallVoidMethod(native_bridge_, callback_method, j_state_str);
      env->DeleteLocalRef(j_state_str);
    }
  }
};

}  // namespace eillance
