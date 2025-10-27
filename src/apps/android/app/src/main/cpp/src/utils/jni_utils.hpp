#pragma once

#include <jni.h>

namespace eillance::jni {

inline auto AttachCurrentThreadIfNeeded(JavaVM* jvm) -> JNIEnv* {
  JNIEnv* env{};
  const jint res = jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  switch (res) {
    case JNI_OK:
      return env;
    case JNI_EDETACHED:
      if (jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
        return env;
      }
      return nullptr;
    default:
      return nullptr;
  }
}

}  // namespace eillance::jni
