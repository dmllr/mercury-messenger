#pragma once

#include <jni.h>
#include <sys/system_properties.h>

#include <atomic>
#include <string>
#include <vector>
#include <cstddef>

#include <nlohmann/json.hpp>

#include "eillance/capture/video_config.hpp"
#include "eillance/logging/logging.h"
#include "eillance/client/client.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/view_model/mercury_view_model.hpp"

#include "class_refs.hpp"
#include "android_persistent_storage.hpp"
#include "transport/transport_impl_libwebrtc.hpp"


namespace eillance {

class ClientEngine {
  JavaVM* jvm_;
  jobject native_bridge_{nullptr};

  std::shared_ptr<Client> client_;
  std::unique_ptr<view_model::MercuryViewModel> view_model_;

public:
  ClientEngine(JavaVM* jvm, JNIEnv* env, jobject instance)
      : jvm_(jvm),
        native_bridge_(env->NewGlobalRef(instance))
  {
    auto storage = std::make_shared<storage::PersistentStorage>(
        std::make_unique<storage::AndroidPersistentStorageImpl>(jvm, instance)
    );

    auto rtc_transport_impl = std::make_unique<transport::AndroidRtcTransport>(jvm, instance);
    auto transport = std::make_shared<transport::Transport>(std::move(rtc_transport_impl));
    client_ = std::make_shared<Client>(storage, transport);

    view_model_ = std::make_unique<view_model::MercuryViewModel>(client_);

    view_model_->SetOnStateChangedCallback([this]() {
      UpdateState();
    });

    std::string device_name;
    auto stored_name = storage->Load("device_name");
    if (stored_name.has_value() && !stored_name->empty()) {
      device_name.assign(reinterpret_cast<const char*>(stored_name->data()), stored_name->size());
    }

    if (device_name.empty()) {
      char model_string[PROP_VALUE_MAX + 1];
      __system_property_get("ro.product.model", model_string);
      device_name = std::string(model_string);
      const auto* data_ptr = reinterpret_cast<const std::byte*>(device_name.data());
      std::vector<std::byte> name_bytes(data_ptr, data_ptr + device_name.size());
      storage->Save("device_name", name_bytes);
    }
    client_->setName(device_name);
  }

  ClientEngine(ClientEngine&&) = delete;
  ClientEngine(const ClientEngine&) = delete;
  auto& operator=(ClientEngine&&) = delete;
  auto& operator=(const ClientEngine&) = delete;

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

  auto ViewModel() -> view_model::MercuryViewModel* {
    return view_model_.get();
  }

  void Start() {
    client_->start();
    LOGI << "Client engine is initialized and ready.";
  }

  void Stop() {}

  void Terminate() {
    Stop();
    client_->terminate();
  }

  void Call(const std::string& peer_id) const {
    client_->call(peer_id);
  }

  void Hangup() const {
    if (view_model_) {
      view_model_->Hangup();
    }
  }

  void ConfigureVideoCapture(const VideoConfig& config) const {
    Config coreConfig;
    coreConfig.video_config = config;
    client_->configure(coreConfig);
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

  void UpdateState() {
    if (native_bridge_ == nullptr) return;
    auto* env = AttachCurrentThreadIfNeeded();
    if (env == nullptr) return;

    jclass callback_class = env->GetObjectClass(native_bridge_);
    jmethodID callback_method = env->GetMethodID(callback_class, "onStateUpdate", "(Ljava/lang/String;)V");
    if (callback_method != nullptr) {
      nlohmann::json state_json = view_model_->GetState();
      std::string state_str = state_json.dump();
      jstring j_state_str = env->NewStringUTF(state_str.c_str());
      env->CallVoidMethod(native_bridge_, callback_method, j_state_str);
      env->DeleteLocalRef(j_state_str);
    }
  }
};

} // namespace eillance
