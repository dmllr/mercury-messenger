#include <cassert>
#include <memory>
#include <string>

#include <jni.h>

#include "android_observers.hpp"
#include "client_engine.hpp"
#include "eillance/view_model/debug_view_model.hpp"

namespace {
JavaVM* java_vm = nullptr;
std::unique_ptr<eillance::ClientEngine> client_engine{};
}  // namespace

// --- JNI Factory Methods ---

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_initEngine(JNIEnv* env, jobject instance) {
  if (client_engine == nullptr) {
    client_engine = std::make_unique<eillance::ClientEngine>(java_vm, env, instance);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_start(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine) {
    client_engine->Start();
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_terminate(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine) {
    client_engine->Terminate();
    client_engine.reset();
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_hela_eillance_CallService_identityExists(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine) {
    return client_engine->GetClient()->IdentityExists();
  }
  return false;
}

// --- ViewModel Creation & Destruction ---

// ContactsViewModel
extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/, jobject observer) {
  if (!client_engine) return 0;
  auto vm = new eillance::view_model::ContactsViewModel(client_engine->GetClient(), client_engine->GetRouter());
  auto android_observer = std::make_shared<eillance::AndroidContactsObserver>(java_vm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  delete vm;
}

// ConversationViewModel
extern "C" JNIEXPORT jlong JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeCreate(
    JNIEnv* env,
    jobject /*instance*/,
    jstring peer_id,
    jobject observer
) {
  if (!client_engine) return 0;
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  auto vm = new eillance::view_model::ConversationViewModel(
      eillance::ContactID(chars),
      client_engine->GetClient(),
      client_engine->GetRouter()
  );
  env->ReleaseStringUTFChars(peer_id, chars);

  auto android_observer = std::make_shared<eillance::AndroidConversationObserver>(java_vm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ConversationViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  delete vm;
}

// ProfileViewModel
extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_ProfileViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/, jobject observer) {
  if (!client_engine) return 0;
  auto vm = new eillance::view_model::ProfileViewModel(client_engine->GetClient(), client_engine->GetRouter());
  auto android_observer = std::make_shared<eillance::AndroidProfileObserver>(java_vm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ProfileViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ProfileViewModel*>(ptr);
  delete vm;
}

// CallViewModel
extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/, jobject observer) {
  if (!client_engine) return 0;
  auto vm = new eillance::view_model::CallViewModel(client_engine->GetClient(), client_engine->GetRouter());
  auto android_observer = std::make_shared<eillance::AndroidCallObserver>(java_vm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_CallViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::CallViewModel*>(ptr);
  delete vm;
}

// SettingsViewModel
extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_SettingsViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/, jobject observer) {
  if (!client_engine) return 0;
  auto vm = new eillance::view_model::SettingsViewModel(client_engine->GetClient(), client_engine->GetRouter());
  auto android_observer = std::make_shared<eillance::AndroidSettingsObserver>(java_vm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_SettingsViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::SettingsViewModel*>(ptr);
  delete vm;
}

// DebugViewModel
extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeCreate(JNIEnv* /*env*/, jobject /*instance*/) {
  if (!client_engine) return 0;
  auto vm = new eillance::view_model::DebugViewModel(client_engine->GetClient(), client_engine->GetRouter());
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  delete vm;
}

// --- ViewModel Method Bindings ---

// Contacts
extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeAddContact(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id,
    jstring token
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
  char const* token_chars = (token != nullptr) ? env->GetStringUTFChars(token, nullptr) : nullptr;
  auto const c_token =
      (token_chars) ? std::optional<std::vector<std::byte>>(eillance::crypto::FromHex(token_chars)) : std::nullopt;
  try {
    vm->AddContact(eillance::ContactID(peer_id_chars), c_token);
  } catch (...) {
  }
  env->ReleaseStringUTFChars(peer_id, peer_id_chars);
  if (token_chars) env->ReleaseStringUTFChars(token, token_chars);
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
  std::vector<std::byte> token = vm->GenerateContactToken();
  return env->NewStringUTF(eillance::crypto::ToHex(token).c_str());
}

// Conversation
extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeSendMessage(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring message
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(message, nullptr);
  vm->SendText(chars);
  env->ReleaseStringUTFChars(message, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeSendMessageWithAttachments(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring message,
    jobjectArray attachments
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* msg_chars = env->GetStringUTFChars(message, nullptr);

  std::vector<eillance::services::AttachmentRequest> atts;
  if (attachments != nullptr) {
    jsize const count = env->GetArrayLength(attachments);
    auto& cls = ClassRefs::Instance().Class("hela/eillance/ndkio/AttachmentRequest");

    for (int i = 0; i < count; ++i) {
      jobject obj = env->GetObjectArrayElement(attachments, i);

      jstring path = (jstring)env->GetObjectField(obj, cls.GetField("path"));
      jint const type = env->GetIntField(obj, cls.GetField("type"));
      jstring thumb = (jstring)env->GetObjectField(obj, cls.GetField("thumbnail"));
      jstring mime = (jstring)env->GetObjectField(obj, cls.GetField("mimeType"));

      char const* p = env->GetStringUTFChars(path, nullptr);
      char const* t = thumb ? env->GetStringUTFChars(thumb, nullptr) : "";
      char const* m = mime ? env->GetStringUTFChars(mime, nullptr) : "";

      atts.push_back(
          {.file_path = p,
           .type = static_cast<eillance::messaging::AttachmentType>(type),
           .thumbnail_base64 = t,
           .mime_type = m}
      );

      env->ReleaseStringUTFChars(path, p);
      if (thumb) {
        env->ReleaseStringUTFChars(thumb, t);
      }
      if (mime) {
        env->ReleaseStringUTFChars(mime, m);
      }
    }
  }

  vm->SendMessage(msg_chars, atts);
  env->ReleaseStringUTFChars(message, msg_chars);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ConversationViewModel_nativeLoadOlder(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  vm->LoadOlderMessages();
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeLoadFull(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jlong msg_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  vm->LoadFullMessage(static_cast<uint64_t>(msg_id));
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeMarkRead(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jlongArray msg_ids
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  if (!msg_ids) return;
  std::vector<std::uint64_t> ids;
  jsize count = env->GetArrayLength(msg_ids);
  jlong* elements = env->GetLongArrayElements(msg_ids, nullptr);
  if (elements) {
    for (jsize i = 0; i < count; i++) ids.push_back(static_cast<uint64_t>(elements[i]));
    env->ReleaseLongArrayElements(msg_ids, elements, JNI_ABORT);
  }
  vm->MarkMessagesAsRead(ids);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeCancelTransfer(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring attachment_id,
    jboolean is_upload
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(attachment_id, nullptr);
  vm->CancelTransfer(chars, is_upload);
  env->ReleaseStringUTFChars(attachment_id, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeResumeTransfer(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring attachment_id,
    jboolean is_upload
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(attachment_id, nullptr);
  vm->ResumeTransfer(chars, is_upload);
  env->ReleaseStringUTFChars(attachment_id, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeDiscardAttachment(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring attachment_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(attachment_id, nullptr);
  vm->DiscardAttachment(chars);
  env->ReleaseStringUTFChars(attachment_id, chars);
}

// Call
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

// Profile
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

// Settings
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

// Debug
extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeEraseHistory(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  vm->EraseAllHistory();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeEraseContacts(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  vm->EraseAllContacts();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_DebugViewModel_nativeRevokeApprovals(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::DebugViewModel*>(ptr);
  vm->RevokeAllApprovals();
}

// --- Global Setup ---

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_CallService_configureVideoCapture(JNIEnv* env, jobject /*instance*/, jobject video_config) {
  if (client_engine) {
    auto& video_config_holder = ClassRefs::Instance().Class("hela/eillance/ndkio/VideoConfig");
    auto conf = eillance::VideoConfig{};
    conf.camera_id = env->GetIntField(video_config, video_config_holder.GetField("cameraId"));
    conf.width = env->GetIntField(video_config, video_config_holder.GetField("width"));
    conf.height = env->GetIntField(video_config, video_config_holder.GetField("height"));
    conf.frame_rate = env->GetIntField(video_config, video_config_holder.GetField("frameRate"));
    conf.bitrate = env->GetIntField(video_config, video_config_holder.GetField("bitrate"));
    client_engine->ConfigureVideoCapture(conf);
  }
}

jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  java_vm = vm;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  Log::InitializeDefault(plog::debug);

  try {
    auto& classref = ClassRefs::Instance();
    classref.RegisterClass(env, "android/content/Context");
    classref.RegisterMethod(env, "android/content/Context", "getFilesDir", "()Ljava/io/File;");
    classref.RegisterClass(env, "java/io/File");
    classref.RegisterMethod(env, "java/io/File", "getPath", "()Ljava/lang/String;");

    std::string const webrtc_transport_class = "hela/eillance/webrtc/WebRtcTransport";
    classref.RegisterClass(env, webrtc_transport_class);
    classref.RegisterCtor(env, webrtc_transport_class, "(Landroid/content/Context;J)V");
    classref.RegisterMethod(env, webrtc_transport_class, "connect", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "terminate", "()V");
    classref.RegisterMethod(env, webrtc_transport_class, "reconnect", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "getPeerConnectionState", "(Ljava/lang/String;)I");
    classref.RegisterMethod(env, webrtc_transport_class, "sendSignalingMessage", "(Ljava/lang/String;)V");
    classref.RegisterMethod(
        env,
        webrtc_transport_class,
        "sendDataChannelMessage",
        "(Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref
        .RegisterMethod(env, webrtc_transport_class, "closePeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "createOffer", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "createAnswer", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(
        env,
        webrtc_transport_class,
        "setRemoteDescription",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterMethod(
        env,
        webrtc_transport_class,
        "addRemoteCandidate",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterMethod(env, webrtc_transport_class, "addVideoTrack", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "addAudioTrack", "(Ljava/lang/String;)V");
    classref
        .RegisterMethod(env, webrtc_transport_class, "createPeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "createDataChannel", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "nativeOnSignalingConnectionStateChange", "(JI)V");

    classref.RegisterClass(env, "hela/eillance/CallService");
    classref.RegisterMethod(
        env,
        "hela/eillance/CallService",
        "setWebRtcTransport",
        "(Lhela/eillance/webrtc/WebRtcTransport;)V"
    );

    // Register ViewModel Observers
    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeContactsObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeContactsObserver",
        "onContactsUpdated",
        "(Ljava/util/List;)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeConversationObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeConversationObserver",
        "onHistoryUpdated",
        "(Lhela/eillance/ndkio/HistoryPage;)V"
    );
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeConversationObserver",
        "onTransferProgress",
        "(Ljava/lang/String;F)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeProfileObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeProfileObserver",
        "onProfileUpdated",
        "(Lhela/eillance/ndkio/MyProfileInfo;)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeCallObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeCallObserver",
        "onCallStateChanged",
        "(Lhela/eillance/ndkio/CallState;)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeSettingsObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeSettingsObserver",
        "onSettingsUpdated",
        "(Ljava/lang/String;I)V"
    );
    classref.RegisterMethod(env, "hela/eillance/viewmodels/NativeSettingsObserver", "onConnectionStateChanged", "(I)V");

    // DTOs
    classref.RegisterCtor(env, "java/util/ArrayList", "()V");
    classref.RegisterMethod(env, "java/util/ArrayList", "add", "(Ljava/lang/Object;)Z");
    classref.RegisterCtor(
        env,
        "hela/eillance/ndkio/MyProfileInfo",
        "(Ljava/lang/String;ZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterCtor(
        env,
        "hela/eillance/ndkio/Contact",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIILjava/lang/String;J)V"
    );
    classref.RegisterCtor(env, "hela/eillance/ndkio/CallState", "(ZZZILjava/lang/String;I)V");

    // Updated HistoryEntry constructor to include attachments list
    classref.RegisterCtor(env, "hela/eillance/ndkio/HistoryEntry", "(JIJLjava/lang/String;IIIIIZLjava/util/List;)V");

    classref.RegisterCtor(env, "hela/eillance/ndkio/HistoryPage", "(Ljava/util/List;Z)V");

    std::string const video_config_class = "hela/eillance/ndkio/VideoConfig";
    classref.RegisterClass(env, video_config_class);
    classref.RegisterField(env, video_config_class, "cameraId", "I");
    classref.RegisterField(env, video_config_class, "width", "I");
    classref.RegisterField(env, video_config_class, "height", "I");
    classref.RegisterField(env, video_config_class, "frameRate", "I");
    classref.RegisterField(env, video_config_class, "bitrate", "I");

    // AttachmentRequest
    std::string const attachment_req_class = "hela/eillance/ndkio/AttachmentRequest";
    classref.RegisterClass(env, attachment_req_class);
    classref.RegisterField(env, attachment_req_class, "path", "Ljava/lang/String;");
    classref.RegisterField(env, attachment_req_class, "type", "I");
    classref.RegisterField(env, attachment_req_class, "thumbnail", "Ljava/lang/String;");
    classref.RegisterField(env, attachment_req_class, "mimeType", "Ljava/lang/String;");

    // Attachment
    std::string const attachment_class = "hela/eillance/ndkio/Attachment";
    classref.RegisterClass(env, attachment_class);
    classref.RegisterCtor(
        env,
        attachment_class,
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;JLjava/lang/String;ZI)V"
    );

  } catch (std::exception const& e) {
    LGE << "JNI_OnLoad failed: " << e.what();
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
