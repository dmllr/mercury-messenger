#include <memory>
#include <vector>

#include <jni.h>

#include "android_observers.hpp"
#include "bridge_context.hpp"
#include "eillance/view_model/conversation_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeCreate(
    JNIEnv* env,
    jobject /*instance*/,
    jstring peer_id,
    jobject observer
) {
  if (!g_clientEngine) {
    return 0;
  }
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  auto* vm = new eillance::view_model::ConversationViewModel(
      eillance::ContactID(chars),
      g_clientEngine->GetClient(),
      g_clientEngine->GetRouter()
  );
  env->ReleaseStringUTFChars(peer_id, chars);

  auto android_observer = std::make_shared<eillance::AndroidConversationObserver>(g_javaVm, observer);
  vm->SetObserver(android_observer);
  return reinterpret_cast<jlong>(vm);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ConversationViewModel_nativeDestroy(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  delete vm;
}

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
      jstring note = (jstring)env->GetObjectField(obj, cls.GetField("note"));
      jstring name = (jstring)env->GetObjectField(obj, cls.GetField("fileName"));

      char const* p = env->GetStringUTFChars(path, nullptr);
      char const* t = thumb ? env->GetStringUTFChars(thumb, nullptr) : "";
      char const* m = mime ? env->GetStringUTFChars(mime, nullptr) : "";
      char const* n = note ? env->GetStringUTFChars(note, nullptr) : "";
      char const* fn = name ? env->GetStringUTFChars(name, nullptr) : "";

      atts.push_back(
          {.file_path = p,
           .type = static_cast<eillance::messaging::AttachmentType>(type),
           .thumbnail_base64 = t,
           .mime_type = m,
           .note = n,
           .file_name = fn}
      );

      env->ReleaseStringUTFChars(path, p);
      if (thumb) {
        env->ReleaseStringUTFChars(thumb, t);
      }
      if (mime) {
        env->ReleaseStringUTFChars(mime, m);
      }
      if (note) {
        env->ReleaseStringUTFChars(note, n);
      }
      if (name) {
        env->ReleaseStringUTFChars(name, fn);
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

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeGetAttachment(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring attachment_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  // We need to access the client from the VM pointer, but VM wraps it. 
  // Since we don't have direct access to Client from VM pointer in C++ easily without a getter,
  // we rely on the global engine instance which is the source of truth anyway.
  if (!g_clientEngine) return nullptr;

  char const* chars = env->GetStringUTFChars(attachment_id, nullptr);
  auto attachment_opt = g_clientEngine->GetClient()->GetAttachment(chars);
  env->ReleaseStringUTFChars(attachment_id, chars);

  if (!attachment_opt) return nullptr;

  auto const& att = *attachment_opt;
  auto& class_refs = ClassRefs::Instance();
  auto& attachment_holder = class_refs.Class("hela/eillance/ndkio/Attachment");

  jstring j_id = env->NewStringUTF(att.id.c_str());
  jstring j_path = env->NewStringUTF(att.file_path.c_str());
  jstring j_mime = env->NewStringUTF(att.mime_type.c_str());
  jstring j_thumb = att.thumbnail_base64.empty() ? nullptr : env->NewStringUTF(att.thumbnail_base64.c_str());
  jstring j_note = env->NewStringUTF(att.note.c_str());
  jstring j_name = env->NewStringUTF(att.file_name.c_str());

  return attachment_holder.NewObject(
      env, j_id, static_cast<jint>(att.type), j_path, j_mime,
      static_cast<jlong>(att.size_bytes), j_thumb,
      static_cast<jboolean>(att.is_downloaded),
      static_cast<jint>(att.status),
      j_note, j_name
  );
}

// NOLINTEND(readability-identifier-naming)
