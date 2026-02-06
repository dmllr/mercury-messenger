#include <memory>
#include <optional>
#include <vector>

#include <jni.h>

#include "android_observers.hpp"
#include "bridge_context.hpp"
#include "eillance/view_model/contacts_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/, jobject observer) {
  if (!g_clientEngine) {
    return 0;
  }
  auto* vm = new eillance::view_model::ContactsViewModel(g_clientEngine->GetClient(), g_clientEngine->GetRouter());
  auto android_observer = std::make_shared<eillance::AndroidContactsObserver>(g_javaVm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  delete vm;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeAddContact(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id,
    jstring token
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
  char const* token_chars = (token != nullptr) ? env->GetStringUTFChars(token, nullptr) : nullptr;
  auto const c_token = ((token_chars) != nullptr)
                           ? std::optional<std::vector<std::byte>>(eillance::crypto::FromHex(token_chars))
                           : std::nullopt;
  bool result = false;
  try {
    result = vm->AddContact(eillance::ContactID(peer_id_chars), c_token);
  } catch (...) {
  }
  env->ReleaseStringUTFChars(peer_id, peer_id_chars);
  if (token_chars != nullptr) {
    env->ReleaseStringUTFChars(token, token_chars);
  }
  return result ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeAcceptContact(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  try {
    vm->AcceptContact(eillance::ContactID(chars));
  } catch (...) {
  }
  env->ReleaseStringUTFChars(peer_id, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeBlockContact(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  try {
    vm->BlockContact(eillance::ContactID(chars));
  } catch (...) {
  }
  env->ReleaseStringUTFChars(peer_id, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeDeleteContact(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  try {
    vm->DeleteContact(eillance::ContactID(chars));
  } catch (...) {
  }
  env->ReleaseStringUTFChars(peer_id, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeUnblockContact(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  try {
    vm->UnblockContact(eillance::ContactID(chars));
  } catch (...) {
  }
  env->ReleaseStringUTFChars(peer_id, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeSetDisplayName(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id,
    jstring name
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* p = env->GetStringUTFChars(peer_id, nullptr);
  char const* n = env->GetStringUTFChars(name, nullptr);
  vm->SetContactDisplayNameOverride(eillance::ContactID(p), n);
  env->ReleaseStringUTFChars(peer_id, p);
  env->ReleaseStringUTFChars(name, n);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeResetDisplayName(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* p = env->GetStringUTFChars(peer_id, nullptr);
  vm->ResetContactDisplayNameOverride(eillance::ContactID(p));
  env->ReleaseStringUTFChars(peer_id, p);
}

extern "C" JNIEXPORT jstring JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeGenerateToken(JNIEnv* env, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  auto const token = vm->GenerateContactToken();
  return env->NewStringUTF(eillance::crypto::ToHex(token).c_str());
}

// NOLINTEND(readability-identifier-naming)
