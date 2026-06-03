#pragma once

#include "utils/class_refs.hpp"

namespace eillance::jni {

inline auto ConvertSingleHistoryEntry(JNIEnv* env, eillance::messaging::HistoryEntryRecord const& entry_cpp)
    -> jobject {
  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jmethodID list_add_method = list_holder.GetMethod("add");
  auto& entry_holder = class_refs.Class("hela/eillance/ndkio/HistoryEntry");
  auto& attachment_holder = class_refs.Class("hela/eillance/ndkio/Attachment");
  auto& reaction_holder = class_refs.Class("hela/eillance/ndkio/ReactionEntry");
  auto& peer_status_holder = class_refs.Class("hela/eillance/ndkio/PeerMessageStatus");

  std::string const* content_str = nullptr;
  std::vector<eillance::messaging::Attachment> const* attachments_ptr = nullptr;
  jlong j_reply_to_id = 0;

  if (auto const* content = eillance::messaging::GetMessageContent(entry_cpp.data)) {
    content_str = &content->content;
    attachments_ptr = &content->attachments;
    j_reply_to_id = static_cast<jlong>(content->reply_to_id);
  }

  auto const* call_entry = std::get_if<eillance::messaging::CallEntry>(&entry_cpp.data);
  jstring content = (content_str != nullptr) ? env->NewStringUTF(content_str->c_str()) : nullptr;
  jint const duration = (call_entry != nullptr) ? static_cast<jint>(call_entry->duration_seconds) : 0;

  jobject j_attachments = list_holder.NewObject(env);
  if ((attachments_ptr != nullptr) && !attachments_ptr->empty()) {
    for (auto const& att : *attachments_ptr) {
      jstring j_id = env->NewStringUTF(att.id.c_str());
      jstring j_path = env->NewStringUTF(att.file_path.c_str());
      jstring j_mime = env->NewStringUTF(att.mime_type.c_str());
      jstring j_thumb = att.thumbnail_base64.empty() ? nullptr : env->NewStringUTF(att.thumbnail_base64.c_str());
      jstring j_note = env->NewStringUTF(att.note.c_str());
      jstring j_name = env->NewStringUTF(att.file_name.c_str());

      jobject j_att = attachment_holder.NewObject(
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

      env->CallBooleanMethod(j_attachments, list_add_method, j_att);
      env->DeleteLocalRef(j_id);
      env->DeleteLocalRef(j_path);
      env->DeleteLocalRef(j_mime);
      if (j_thumb != nullptr) env->DeleteLocalRef(j_thumb);
      env->DeleteLocalRef(j_note);
      env->DeleteLocalRef(j_name);
      env->DeleteLocalRef(j_att);
    }
  }

  jobject j_reactions = list_holder.NewObject(env);
  for (auto const& r : entry_cpp.reactions) {
    jstring r_auth = env->NewStringUTF(r.author_id.ToString().c_str());
    jobject j_react = reaction_holder.NewObject(env, r_auth, static_cast<jint>(r.reaction));
    env->CallBooleanMethod(j_reactions, list_add_method, j_react);
    env->DeleteLocalRef(r_auth);
    env->DeleteLocalRef(j_react);
  }

  jobject j_peer_statuses = list_holder.NewObject(env);
  if (auto const* group_entry = std::get_if<eillance::messaging::GroupMessageEntry>(&entry_cpp.data)) {
    for (auto const& ps : group_entry->peer_statuses) {
      jstring p_id = env->NewStringUTF(ps.peer_id.ToString().c_str());
      jobject j_ps = peer_status_holder.NewObject(env, p_id, static_cast<jint>(ps.status));
      env->CallBooleanMethod(j_peer_statuses, list_add_method, j_ps);
      env->DeleteLocalRef(p_id);
      env->DeleteLocalRef(j_ps);
    }
  }

  jstring j_author_id = env->NewStringUTF(entry_cpp.author_id.ToString().c_str());

  jobject entry_java = entry_holder.NewObject(
      env,
      static_cast<jlong>(entry_cpp.id),
      static_cast<jint>(entry_cpp.kind),
      static_cast<jlong>(entry_cpp.timestamp),
      content,
      static_cast<jint>(entry_cpp.direction),
      duration,
      (call_entry != nullptr) ? static_cast<jint>(call_entry->call_direction) : -1,
      (call_entry != nullptr) ? static_cast<jint>(call_entry->call_status) : -1,
      static_cast<jint>(entry_cpp.status),
      static_cast<jboolean>(entry_cpp.is_truncated),
      static_cast<jboolean>(entry_cpp.is_edited),
      static_cast<jboolean>(entry_cpp.is_deleted),
      j_attachments,
      j_reactions,
      j_reply_to_id,
      j_peer_statuses,
      j_author_id
  );

  if (content != nullptr) env->DeleteLocalRef(content);
  env->DeleteLocalRef(j_attachments);
  env->DeleteLocalRef(j_reactions);
  env->DeleteLocalRef(j_peer_statuses);
  env->DeleteLocalRef(j_author_id);

  return entry_java;
}

template <typename Container>
inline auto ConvertHistoryEntriesImpl(JNIEnv* env, Container const& messages) -> jobject {
  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject history_list = list_holder.NewObject(env);
  jmethodID list_add_method = list_holder.GetMethod("add");
  for (auto const& entry_cpp : messages) {
    jobject entry_java = ConvertSingleHistoryEntry(env, entry_cpp);
    env->CallBooleanMethod(history_list, list_add_method, entry_java);
    env->DeleteLocalRef(entry_java);
  }
  return history_list;
}

inline auto ConvertHistoryEntries(JNIEnv* env, std::vector<eillance::messaging::HistoryEntryRecord> const& messages)
    -> jobject {
  return ConvertHistoryEntriesImpl(env, messages);
}

inline auto ConvertHistoryEntries(JNIEnv* env, std::deque<eillance::messaging::HistoryEntryRecord> const& messages)
    -> jobject {
  return ConvertHistoryEntriesImpl(env, messages);
}

}  // namespace eillance::jni
