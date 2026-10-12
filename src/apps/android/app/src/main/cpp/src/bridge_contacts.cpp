#include <memory>
#include <optional>
#include <vector>

#include <jni.h>

#include "bridge_context.hpp"
#include "eillance/view_model/contacts_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/) {
  if (!g_clientEngine) {
    return 0;
  }
  auto* vm = new eillance::view_model::ContactsViewModel(g_clientEngine->GetClient());
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

extern "C" JNIEXPORT jstring JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeCreateGroup(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring name,
    jstring peer_ids_csv
) {
  if (!g_clientEngine) return nullptr;

  char const* name_chars = env->GetStringUTFChars(name, nullptr);
  char const* csv_chars = env->GetStringUTFChars(peer_ids_csv, nullptr);

  std::string group_name(name_chars);
  std::string csv(csv_chars);

  env->ReleaseStringUTFChars(name, name_chars);
  env->ReleaseStringUTFChars(peer_ids_csv, csv_chars);

  std::vector<eillance::ContactID> members;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) {
      members.emplace_back(item);
    }
  }

  auto gid = g_clientEngine->GetClient()->CreateGroup(group_name, members);
  return env->NewStringUTF(gid.ToString().c_str());
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeGetGroupMembers(
    JNIEnv* env, jobject, jlong ptr, jstring group_id) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* gid_chars = env->GetStringUTFChars(group_id, nullptr);
  auto members = vm->GetGroupMembers(gid_chars);
  env->ReleaseStringUTFChars(group_id, gid_chars);

  auto& class_refs = ClassRefs::Instance();
  auto& gm_class = class_refs.Class("hela/eillance/ndkio/GroupMember");
  jobjectArray j_members = env->NewObjectArray(static_cast<jsize>(members.size()), gm_class.Class(), nullptr);

  for (size_t i = 0; i < members.size(); ++i) {
    jstring pid = env->NewStringUTF(members[i].contact_id.ToString().c_str());
    jobject j_mem = gm_class.NewObject(env, pid, static_cast<jint>(members[i].role));
    env->SetObjectArrayElement(j_members, static_cast<jsize>(i), j_mem);
    env->DeleteLocalRef(pid);
    env->DeleteLocalRef(j_mem);
  }
  return j_members;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeAddGroupMember(
    JNIEnv* env, jobject, jlong ptr, jstring group_id, jstring contact_id) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* gid_chars = env->GetStringUTFChars(group_id, nullptr);
  char const* cid_chars = env->GetStringUTFChars(contact_id, nullptr);
  bool res = vm->AddGroupMember(gid_chars, cid_chars);
  env->ReleaseStringUTFChars(group_id, gid_chars);
  env->ReleaseStringUTFChars(contact_id, cid_chars);
  return res ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeRemoveGroupMember(
    JNIEnv* env, jobject, jlong ptr, jstring group_id, jstring contact_id) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* gid_chars = env->GetStringUTFChars(group_id, nullptr);
  char const* cid_chars = env->GetStringUTFChars(contact_id, nullptr);
  bool res = vm->RemoveGroupMember(gid_chars, cid_chars);
  env->ReleaseStringUTFChars(group_id, gid_chars);
  env->ReleaseStringUTFChars(contact_id, cid_chars);
  return res ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeLeaveGroup(
    JNIEnv* env, jobject, jlong ptr, jstring group_id) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* gid_chars = env->GetStringUTFChars(group_id, nullptr);
  vm->LeaveGroup(gid_chars);
  env->ReleaseStringUTFChars(group_id, gid_chars);
}

extern "C" JNIEXPORT jstring JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeGenerateToken(JNIEnv* env, jobject /*instance*/, jlong ptr) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  auto const token = vm->GenerateContactToken();
  return env->NewStringUTF(eillance::crypto::ToHex(token).c_str());
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeMarkAllAsRead(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  vm->MarkAllAsRead(eillance::ContactID(chars));
  env->ReleaseStringUTFChars(peer_id, chars);
}

static jobject
ConvertHistoryEntries(JNIEnv* env, std::vector<eillance::messaging::HistoryEntryRecord> const& messages) {
  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject history_list = list_holder.NewObject(env);
  jmethodID list_add_method = list_holder.GetMethod("add");
  auto& entry_holder = class_refs.Class("hela/eillance/ndkio/HistoryEntry");
  auto& attachment_holder = class_refs.Class("hela/eillance/ndkio/Attachment");
  auto& reaction_holder = class_refs.Class("hela/eillance/ndkio/ReactionEntry");

  for (auto const& entry_cpp : messages) {
    std::string const* content_str = nullptr;
    std::vector<eillance::messaging::Attachment> const* attachments_ptr = nullptr;
    jlong j_reply_to_id = 0;

    if (std::holds_alternative<eillance::messaging::MessageEntry>(entry_cpp.data)) {
      auto const& msg = std::get<eillance::messaging::MessageEntry>(entry_cpp.data);
      content_str = &msg.content;
      attachments_ptr = &msg.attachments;
      j_reply_to_id = msg.reply_to_id;
    }

    auto const* call_entry = std::get_if<eillance::messaging::CallEntry>(&entry_cpp.data);
    jstring content = content_str ? env->NewStringUTF(content_str->c_str()) : nullptr;
    jint const duration = call_entry ? static_cast<jint>(call_entry->duration_seconds) : 0;

    // Attachments
    jobject j_attachments = list_holder.NewObject(env);
    if (attachments_ptr && !attachments_ptr->empty()) {
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
        if (j_thumb != nullptr) {
          env->DeleteLocalRef(j_thumb);
        }
        env->DeleteLocalRef(j_note);
        env->DeleteLocalRef(j_name);
        env->DeleteLocalRef(j_att);
      }
    }

    // Reactions
    jobject j_reactions = list_holder.NewObject(env);
    for (auto const& r : entry_cpp.reactions) {
      jstring r_auth = env->NewStringUTF(r.author_id.ToString().c_str());
      jobject j_react = reaction_holder.NewObject(env, r_auth, static_cast<jint>(r.reaction));
      env->CallBooleanMethod(j_reactions, list_add_method, j_react);
      env->DeleteLocalRef(r_auth);
      env->DeleteLocalRef(j_react);
    }

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
        j_reply_to_id
    );

    env->CallBooleanMethod(history_list, list_add_method, entry_java);
    if (content != nullptr) {
      env->DeleteLocalRef(content);
    }
    env->DeleteLocalRef(j_attachments);
    env->DeleteLocalRef(j_reactions);
    env->DeleteLocalRef(entry_java);
  }
  return history_list;
}

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeGetUnreadMessages(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  auto messages = vm->GetUnreadMessages(eillance::ContactID(chars));
  env->ReleaseStringUTFChars(peer_id, chars);
  return ConvertHistoryEntries(env, messages);
}

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeSearchAllMessages(
    JNIEnv* env,
    jobject /*instance*/,
    jlong /*ptr*/,
    jstring query
) {
  if (!g_clientEngine) return nullptr;
  char const* chars = env->GetStringUTFChars(query, nullptr);
  auto messages = g_clientEngine->GetClient()->SearchAllMessages(chars);
  env->ReleaseStringUTFChars(query, chars);

  // Convert to GlobalSearchResult list
  auto& class_refs = ClassRefs::Instance();
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject result_list = list_holder.NewObject(env);
  jmethodID list_add_method = list_holder.GetMethod("add");
  auto& search_result_holder = class_refs.Class("hela/eillance/ndkio/GlobalSearchResult");

  for (auto const& msg : messages) {
    // Create single-item vector to reuse conversion logic (inefficient but reuses code)
    auto const single_vec = std::vector<eillance::messaging::HistoryEntryRecord>{msg};
    jobject single_list = ConvertHistoryEntries(env, single_vec);  // ArrayList<HistoryEntry>

    // Get the item
    auto* get_method = list_holder.GetMethod("get");
    auto* entry_java = env->CallObjectMethod(single_list, get_method, 0);

    jstring peer_id = env->NewStringUTF(msg.peer_id.ToString().c_str());
    jobject global_result = search_result_holder.NewObject(env, peer_id, entry_java);

    env->CallBooleanMethod(result_list, list_add_method, global_result);

    env->DeleteLocalRef(single_list);
    env->DeleteLocalRef(entry_java);
    env->DeleteLocalRef(peer_id);
    env->DeleteLocalRef(global_result);
  }

  return result_list;
}

// NOLINTEND(readability-identifier-naming)
