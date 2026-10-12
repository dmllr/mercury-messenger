#pragma once

#include <jni.h>

namespace eillance::jni {

class ScopedJniEnv {
  JavaVM* jvm_;
  JNIEnv* env_{nullptr};
  bool attached_{false};

 public:
  explicit ScopedJniEnv(JavaVM* jvm)
      : jvm_(jvm) {
    if (jvm_ == nullptr) {
      return;
    }
    jint const res = jvm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
    if (res == JNI_EDETACHED) {
      if (jvm_->AttachCurrentThread(&env_, nullptr) == JNI_OK) {
        attached_ = true;
      }
    } else if (res == JNI_OK) {
      // Already attached, env_ is set
    }
  }

  ScopedJniEnv(ScopedJniEnv const&) = delete;
  auto operator=(ScopedJniEnv const&) -> ScopedJniEnv& = delete;

  ~ScopedJniEnv() {
    if (attached_ && (jvm_ != nullptr)) {
      jvm_->DetachCurrentThread();
    }
  }

  [[nodiscard]] auto Get() const -> JNIEnv* {
    return env_;
  }
  [[nodiscard]] auto IsValid() const -> bool {
    return env_ != nullptr;
  }
};

}  // namespace eillance::jni
