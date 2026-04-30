#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <jni.h>
#include <sys/system_properties.h>

#include "eillance/client/client.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "media_transport_impl_libwebrtc.hpp"
#include "utils/class_refs.hpp"

namespace eillance {

class ClientEngine {
  std::shared_ptr<Client> client_;
  JavaVM* jvm_;
  jobject service_ref_;
  std::vector<uint64_t> event_unsubs_;

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

    auto media_transport = std::make_unique<transport::AndroidMediaTransport>(jvm, context);

    client_ = std::make_shared<Client>(
        storage,
        std::make_shared<transport::MediaTransport>(std::move(media_transport)),
        nullptr,
        transport::NodeRole::mobileClient
    );

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventFatalError>([this](auto const&) -> void {
      jni::ScopedJniEnv const env_scope(jvm_);
      if (auto* e = env_scope.Get()) {
        auto const& cls = ClassRefs::Instance().Class("hela/eillance/MessengerService");
        jmethodID method = cls.GetMethod("onNativeCoreCrash");
        e->CallVoidMethod(service_ref_, method);
      }
    }));
  }

  ClientEngine(ClientEngine&&) = delete;
  ClientEngine(ClientEngine const&) = delete;
  auto operator=(ClientEngine&&) -> ClientEngine& = delete;
  auto operator=(ClientEngine const&) -> ClientEngine& = delete;
  ~ClientEngine() {
    for (auto id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
    if (service_ref_ != nullptr) {
      jni::ScopedJniEnv const env_scope(jvm_);
      if (auto* env = env_scope.Get()) {
        env->DeleteGlobalRef(service_ref_);
      }
    }
    Terminate();
  }

  [[nodiscard]] auto GetClient() const -> std::shared_ptr<Client> {
    return client_;
  }

  void Start() {
    client_->Start();
    LGI << "Client engine is initialized and ready.";
  }

  void Terminate() {
    client_->Terminate();
  }
};

}  // namespace eillance
