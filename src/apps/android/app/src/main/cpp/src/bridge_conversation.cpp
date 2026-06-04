#include <memory>
#include <vector>

#include <jni.h>

#include "bridge_context.hpp"
#include "bridge_conversions.hpp"
#include "eillance/view_model/conversation_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming,*-pro-type-reinterpret-cast)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_ConversationViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/, jstring peer_id) {
  auto engine = GetSafeEngine();
  if (!engine) {
    return 0;
  }
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  auto* vm = new eillance::view_model::ConversationViewModel(eillance::TopicID(chars), engine->GetClient());
  env->ReleaseStringUTFChars(peer_id, chars);

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
    jstring message,
    jlong reply_to_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(message, nullptr);
  vm->SendMessage(chars, static_cast<uint64_t>(reply_to_id));
  env->ReleaseStringUTFChars(message, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeAddDraftAttachment(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring path,
    jint type,
    jstring mime,
    jstring name,
    jstring thumb
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);

  char const* p = env->GetStringUTFChars(path, nullptr);
  char const* m = env->GetStringUTFChars(mime, nullptr);
  char const* n = env->GetStringUTFChars(name, nullptr);
  char const* t = env->GetStringUTFChars(thumb, nullptr);

  vm->AddDraftAttachment(
      {.file_path = p,
       .type = static_cast<eillance::messaging::AttachmentType>(type),
       .mime_type = m,
       .file_name = n,
       .thumbnail_base64 = t,
       .note = ""}
  );

  env->ReleaseStringUTFChars(path, p);
  env->ReleaseStringUTFChars(mime, m);
  env->ReleaseStringUTFChars(name, n);
  env->ReleaseStringUTFChars(thumb, t);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeRemoveDraftAttachment(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jint index
) {
  reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr)->RemoveDraftAttachment(index);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeUpdateDraftNote(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jint index,
    jstring note
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* n = env->GetStringUTFChars(note, nullptr);
  vm->UpdateDraftNote(index, n);
  env->ReleaseStringUTFChars(note, n);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeOnMessageShown(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jlong msg_id
) {
  reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr)->OnMessageShown(static_cast<uint64_t>(msg_id));
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeDeleteMessage(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jlong msg_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  vm->DeleteMessage(static_cast<uint64_t>(msg_id));
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeEditMessage(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jlong msg_id,
    jstring content
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(content, nullptr);
  vm->EditMessage(static_cast<uint64_t>(msg_id), chars);
  env->ReleaseStringUTFChars(content, chars);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeReactMessage(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jlong msg_id,
    jint reaction
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  vm->ReactToMessage(static_cast<uint64_t>(msg_id), static_cast<eillance::messaging::Reaction>(reaction));
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ConversationViewModel_nativeLoadOlder(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  vm->LoadOlderMessages();
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeLoadUpTo(
    JNIEnv* /*env*/,
    jobject /*instance*/,
    jlong ptr,
    jlong msg_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  vm->LoadUpToMessage(static_cast<uint64_t>(msg_id));
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
  if (msg_ids == nullptr) {
    return;
  }
  std::vector<std::uint64_t> ids;
  jsize count = env->GetArrayLength(msg_ids);
  jlong* elements = env->GetLongArrayElements(msg_ids, nullptr);
  if (elements != nullptr) {
    for (jsize i = 0; i < count; i++) {
      ids.push_back(static_cast<uint64_t>(elements[i]));
    }
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
  vm->CancelTransfer(chars, is_upload != 0U);
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
  vm->ResumeTransfer(chars, is_upload != 0U);
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
  auto engine = GetSafeEngine();
  if (!engine) {
    return nullptr;
  }

  char const* chars = env->GetStringUTFChars(attachment_id, nullptr);
  auto attachment_opt = engine->GetClient()->GetAttachment(chars);
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
      env,
      j_id,
      static_cast<jint>(att.type),
      j_path,
      j_mime,
      static_cast<jlong>(att.size_bytes),
      j_thumb,
      static_cast<jboolean>(att.is_downloaded),
      static_cast<jint>(att.status),
      j_note,
      j_name
  );
}

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeSearchMessages(
    JNIEnv* env,
    jobject /*instance*/,
    jlong /*ptr*/,
    jstring peer_id,
    jstring query
) {
  auto engine = GetSafeEngine();
  if (!engine) return nullptr;

  char const* p_chars = env->GetStringUTFChars(peer_id, nullptr);
  char const* q_chars = env->GetStringUTFChars(query, nullptr);

  eillance::TopicID const pid(p_chars);
  auto messages = engine->GetClient()->SearchMessages(pid, q_chars);

  env->ReleaseStringUTFChars(peer_id, p_chars);
  env->ReleaseStringUTFChars(query, q_chars);
  return eillance::jni::ConvertHistoryEntries(env, messages);
}

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeGetHistoryEntry(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jlong msg_id
) {
  auto engine = GetSafeEngine();
  if (!engine) return nullptr;
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  auto entry_opt = vm->GetHistoryEntry(msg_id);
  if (!entry_opt) return nullptr;
  return eillance::jni::ConvertSingleHistoryEntry(env, *entry_opt);
}

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeGetDownloadedAttachments(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr
) {
  auto engine = GetSafeEngine();
  if (!engine) return nullptr;
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  auto records = vm->GetDownloadedAttachments();

  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject result_list = list_holder.NewObject(env);
  jmethodID list_add = list_holder.GetMethod("add");

  auto& info_holder = class_refs.Class("hela/eillance/ndkio/AttachmentInfo");
  auto& att_holder = class_refs.Class("hela/eillance/ndkio/Attachment");

  for (auto const& rec : records) {
    auto const& att = rec.attachment;
    jstring j_id = env->NewStringUTF(att.id.c_str());
    jstring j_path = env->NewStringUTF(att.file_path.c_str());
    jstring j_mime = env->NewStringUTF(att.mime_type.c_str());
    jstring j_thumb = att.thumbnail_base64.empty() ? nullptr : env->NewStringUTF(att.thumbnail_base64.c_str());
    jstring j_note = env->NewStringUTF(att.note.c_str());
    jstring j_name = env->NewStringUTF(att.file_name.c_str());

    jobject j_att = att_holder.NewObject(
        env,
        j_id,
        static_cast<jint>(att.type),
        j_path,
        j_mime,
        static_cast<jlong>(att.size_bytes),
        j_thumb,
        static_cast<jboolean>(att.is_downloaded),
        static_cast<jint>(att.status),
        j_note,
        j_name
    );
    jobject j_info = info_holder.NewObject(env, j_att, static_cast<jlong>(rec.timestamp));
    env->CallBooleanMethod(result_list, list_add, j_info);

    env->DeleteLocalRef(j_id);
    env->DeleteLocalRef(j_path);
    env->DeleteLocalRef(j_mime);
    if (j_thumb) env->DeleteLocalRef(j_thumb);
    env->DeleteLocalRef(j_note);
    env->DeleteLocalRef(j_name);
    env->DeleteLocalRef(j_att);
    env->DeleteLocalRef(j_info);
  }
  return result_list;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ConversationViewModel_nativeDeleteAttachmentLocal(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring attachment_id
) {
  auto engine = GetSafeEngine();
  if (!engine) return;
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(attachment_id, nullptr);
  vm->DeleteAttachmentLocal(chars);
  env->ReleaseStringUTFChars(attachment_id, chars);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ConversationViewModel_nativeRetrySync(JNIEnv* /*env*/, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ConversationViewModel*>(ptr);
  vm->RetrySync();
}

// NOLINTEND(readability-identifier-naming,*-pro-type-reinterpret-cast)
