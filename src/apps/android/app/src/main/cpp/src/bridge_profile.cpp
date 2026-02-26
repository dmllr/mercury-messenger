#include <memory>
#include <optional>
#include <string>

#include <jni.h>

#include "bridge_context.hpp"
#include "eillance/view_model/profile_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_ProfileViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/) {
  if (!g_clientEngine) {
    return 0;
  }
  auto* vm = new eillance::view_model::ProfileViewModel(g_clientEngine->GetClient());
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ProfileViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  delete vm;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ProfileViewModel_nativeUpdateProfile(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring name,
    jstring bio,
    jstring photo
) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  char const* n = name ? env->GetStringUTFChars(name, nullptr) : nullptr;
  char const* b = bio ? env->GetStringUTFChars(bio, nullptr) : nullptr;
  char const* p = photo ? env->GetStringUTFChars(photo, nullptr) : nullptr;

  vm->UpdateProfile(
      n ? std::optional<std::string>(n) : std::nullopt,
      b ? std::optional<std::string>(b) : std::nullopt,
      p ? std::optional<std::string>(p) : std::nullopt
  );

  if (n) env->ReleaseStringUTFChars(name, n);
  if (b) env->ReleaseStringUTFChars(bio, b);
  if (p) env->ReleaseStringUTFChars(photo, p);
}

extern "C" JNIEXPORT jstring JNICALL
Java_hela_eillance_viewmodels_ProfileViewModel_nativeGenerateIdentity(JNIEnv* env, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  return env->NewStringUTF(vm->GenerateNewIdentity().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ProfileViewModel_nativeRestoreIdentity(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring mnemonic
) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(mnemonic, nullptr);
  bool res = vm->RestoreIdentity(chars);
  env->ReleaseStringUTFChars(mnemonic, chars);
  return res;
}

extern "C" JNIEXPORT jstring JNICALL
Java_hela_eillance_viewmodels_ProfileViewModel_nativeGetMnemonic(JNIEnv* env, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  if (auto m = vm->GetMnemonic()) {
    return env->NewStringUTF(m->c_str());
  }
  return nullptr;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ProfileViewModel_nativeCreateBackup(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring path,
    jstring password
) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  char const* p = env->GetStringUTFChars(path, nullptr);
  char const* pw = env->GetStringUTFChars(password, nullptr);
  bool res = vm->CreateBackup(p, pw);
  env->ReleaseStringUTFChars(path, p);
  env->ReleaseStringUTFChars(password, pw);
  return res;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ProfileViewModel_nativeRestoreBackup(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring path,
    jstring password
) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  char const* p = env->GetStringUTFChars(path, nullptr);
  char const* pw = env->GetStringUTFChars(password, nullptr);
  bool res = vm->RestoreBackup(p, pw);
  env->ReleaseStringUTFChars(path, p);
  env->ReleaseStringUTFChars(password, pw);
  return res;
}

// NOLINTEND(readability-identifier-naming)
