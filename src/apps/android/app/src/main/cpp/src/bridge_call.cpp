#include <memory>

#include <jni.h>

#include "bridge_context.hpp"
#include "eillance/view_model/call_view_model.hpp"

// NOLINTNEXTLINE(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/) {
  if (!g_clientEngine) {
    return 0;
  }
  auto* vm = new eillance::view_model::CallViewModel(g_clientEngine->GetClient());
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::CallViewModel*>(ptr);
  delete vm;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_CallViewModel_nativeStartVoice(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::CallViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  vm->StartVoiceCall(eillance::ContactID(chars));
  env->ReleaseStringUTFChars(peer_id, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_CallViewModel_nativeStartVideo(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::CallViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  vm->StartVideoCall(eillance::ContactID(chars));
  env->ReleaseStringUTFChars(peer_id, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeHangup(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::CallViewModel*>(ptr);
  vm->Hangup();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeAccept(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::CallViewModel*>(ptr);
  vm->AcceptCall();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeReject(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::CallViewModel*>(ptr);
  vm->RejectCall();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeToggleMute(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  reinterpret_cast<eillance::view_model::CallViewModel*>(ptr)->ToggleMute();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeToggleSpeaker(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  reinterpret_cast<eillance::view_model::CallViewModel*>(ptr)->ToggleSpeaker();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeStopCallMedia(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  reinterpret_cast<eillance::view_model::CallViewModel*>(ptr)->StopCallMedia();
}

// NOLINTEND(readability-identifier-naming)
