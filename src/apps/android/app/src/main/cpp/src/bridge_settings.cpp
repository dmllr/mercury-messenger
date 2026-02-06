#include <memory>

#include <jni.h>

#include "android_observers.hpp"
#include "bridge_context.hpp"
#include "eillance/view_model/settings_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_SettingsViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/, jobject observer) {
  if (!g_clientEngine) {
    return 0;
  }
  auto* vm = new eillance::view_model::SettingsViewModel(g_clientEngine->GetClient(), g_clientEngine->GetRouter());
  auto android_observer = std::make_shared<eillance::AndroidSettingsObserver>(g_javaVm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_SettingsViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  delete vm;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_SettingsViewModel_nativeUpdateUrl(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring url
) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(url, nullptr);
  vm->UpdateSignalingUrl(chars);
  env->ReleaseStringUTFChars(url, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_SettingsViewModel_nativeSetTimeout(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jint seconds
) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  vm->SetPeerInactivityTimeout(seconds);
}

// NOLINTEND(readability-identifier-naming)
