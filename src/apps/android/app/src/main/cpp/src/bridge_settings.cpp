#include <memory>

#include <jni.h>

#include "bridge_context.hpp"
#include "eillance/view_model/settings_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_SettingsViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/) {
  if (!g_clientEngine) {
    return 0;
  }
  auto* vm = new eillance::view_model::SettingsViewModel(g_clientEngine->GetClient());
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_SettingsViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  delete vm;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_SettingsViewModel_nativeUpdateBootstrapNodeUrl(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring url
) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(url, nullptr);
  vm->UpdateBootstrapNodeUrl(chars);
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

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_SettingsViewModel_nativeUpdateIceConfig(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring stun,
    jstring turn,
    jstring user,
    jstring pass
) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  char const* s = env->GetStringUTFChars(stun, nullptr);
  char const* t = env->GetStringUTFChars(turn, nullptr);
  char const* u = env->GetStringUTFChars(user, nullptr);
  char const* p = env->GetStringUTFChars(pass, nullptr);

  vm->UpdateIceConfig({.stun = s ? s : "", .turn = t ? t : "", .turn_user = u ? u : "", .turn_pass = p ? p : ""});

  env->ReleaseStringUTFChars(stun, s);
  env->ReleaseStringUTFChars(turn, t);
  env->ReleaseStringUTFChars(user, u);
  env->ReleaseStringUTFChars(pass, p);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_SettingsViewModel_nativeSetSoundSettings(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jboolean send,
    jboolean receive
) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  vm->SetSoundSettings(send != 0U, receive != 0U);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_SettingsViewModel_nativeSetShowNotificationContent(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jboolean show
) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  vm->SetShowNotificationContent(show != 0U);
}

// NOLINTEND(readability-identifier-naming)
