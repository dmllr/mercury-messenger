#include <memory>
#include <optional>
#include <vector>

#include <jni.h>

#include "bridge_context.hpp"
#include "bridge_conversions.hpp"
#include "eillance/view_model/contacts_view_model.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT jlong JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeCreate(JNIEnv* env, jobject /*instance*/) {
  auto engine = GetSafeEngine();
  if (!engine) {
    return 0;
  }
  auto* vm = new eillance::view_model::ContactsViewModel(engine->GetClient());
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
  auto engine = GetSafeEngine();
  if (!engine) return nullptr;

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

  auto gid = engine->GetClient()->CreateGroup(group_name, members);
  return env->NewStringUTF(gid.ToString().c_str());
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeGetGroupMembers(
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring group_id
) {
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
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring group_id,
    jstring contact_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* gid_chars = env->GetStringUTFChars(group_id, nullptr);
  char const* cid_chars = env->GetStringUTFChars(contact_id, nullptr);
  bool res = vm->AddGroupMember(gid_chars, cid_chars);
  env->ReleaseStringUTFChars(group_id, gid_chars);
  env->ReleaseStringUTFChars(contact_id, cid_chars);
  return res ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeRemoveGroupMember(
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring group_id,
    jstring contact_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* gid_chars = env->GetStringUTFChars(group_id, nullptr);
  char const* cid_chars = env->GetStringUTFChars(contact_id, nullptr);
  bool res = vm->RemoveGroupMember(gid_chars, cid_chars);
  env->ReleaseStringUTFChars(group_id, gid_chars);
  env->ReleaseStringUTFChars(contact_id, cid_chars);
  return res ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeLeaveGroup(JNIEnv* env, jobject, jlong ptr, jstring group_id) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* gid_chars = env->GetStringUTFChars(group_id, nullptr);
  vm->LeaveGroup(gid_chars);
  env->ReleaseStringUTFChars(group_id, gid_chars);
}

extern "C" JNIEXPORT jstring JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeCreateChannel(
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring name,
    jstring desc
) {
  auto engine = GetSafeEngine();
  if (!engine) return nullptr;
  char const* n = env->GetStringUTFChars(name, nullptr);
  char const* d = env->GetStringUTFChars(desc, nullptr);
  auto cid = engine->GetClient()->CreateChannel(n, d);
  env->ReleaseStringUTFChars(name, n);
  env->ReleaseStringUTFChars(desc, d);
  return env->NewStringUTF(cid.ToString().c_str());
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeSubscribeToChannel(
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring cid,
    jstring oid
) {
  auto engine = GetSafeEngine();
  if (!engine) return;
  char const* c = env->GetStringUTFChars(cid, nullptr);
  char const* o = env->GetStringUTFChars(oid, nullptr);
  engine->GetClient()->SubscribeToChannel(eillance::ChannelID(c), eillance::ContactID(o));
  env->ReleaseStringUTFChars(cid, c);
  env->ReleaseStringUTFChars(oid, o);
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_viewmodels_ContactsViewModel_nativeUnsubscribeChannel(JNIEnv* env, jobject, jlong ptr, jstring cid) {
  auto engine = GetSafeEngine();
  if (!engine) return;
  char const* c = env->GetStringUTFChars(cid, nullptr);
  engine->GetClient()->UnsubscribeChannel(eillance::ChannelID(c));
  env->ReleaseStringUTFChars(cid, c);
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeGetChannelMembers(
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring channel_id
) {
  auto engine = GetSafeEngine();
  if (!engine) return nullptr;
  char const* cid_chars = env->GetStringUTFChars(channel_id, nullptr);
  auto members = engine->GetClient()->GetChannelMembers(eillance::ChannelID(cid_chars));
  env->ReleaseStringUTFChars(channel_id, cid_chars);

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

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeAddChannelMember(
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring cid,
    jstring mid
) {
  auto engine = GetSafeEngine();
  if (!engine) return false;
  char const* c = env->GetStringUTFChars(cid, nullptr);
  char const* m = env->GetStringUTFChars(mid, nullptr);
  bool res = engine->GetClient()->AddChannelMember(eillance::ChannelID(c), eillance::ContactID(m));
  env->ReleaseStringUTFChars(cid, c);
  env->ReleaseStringUTFChars(mid, m);
  return res ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeRemoveChannelMember(
    JNIEnv* env,
    jobject,
    jlong ptr,
    jstring cid,
    jstring mid
) {
  auto engine = GetSafeEngine();
  if (!engine) return false;
  char const* c = env->GetStringUTFChars(cid, nullptr);
  char const* m = env->GetStringUTFChars(mid, nullptr);
  bool res = engine->GetClient()->RemoveChannelMember(eillance::ChannelID(c), eillance::ContactID(m));
  env->ReleaseStringUTFChars(cid, c);
  env->ReleaseStringUTFChars(mid, m);
  return res ? JNI_TRUE : JNI_FALSE;
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
  vm->MarkAllAsRead(eillance::TopicID(chars));
  env->ReleaseStringUTFChars(peer_id, chars);
}

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeGetUnreadMessages(
    JNIEnv* env,
    jobject /*instance*/,
    jlong ptr,
    jstring peer_id
) {
  auto* vm = reinterpret_cast<eillance::view_model::ContactsViewModel*>(ptr);
  char const* chars = env->GetStringUTFChars(peer_id, nullptr);
  auto messages = vm->GetUnreadMessages(eillance::TopicID(chars));
  env->ReleaseStringUTFChars(peer_id, chars);
  return eillance::jni::ConvertHistoryEntries(env, messages);
}

extern "C" JNIEXPORT jobject JNICALL Java_hela_eillance_viewmodels_ContactsViewModel_nativeSearchAllMessages(
    JNIEnv* env,
    jobject /*instance*/,
    jlong /*ptr*/,
    jstring query
) {
  auto engine = GetSafeEngine();
  if (!engine) return nullptr;
  char const* chars = env->GetStringUTFChars(query, nullptr);
  auto messages = engine->GetClient()->SearchAllMessages(chars);
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
    jobject single_list = eillance::jni::ConvertHistoryEntries(env, single_vec);  // ArrayList<HistoryEntry>

    // Get the item
    auto* get_method = list_holder.GetMethod("get");
    auto* entry_java = env->CallObjectMethod(single_list, get_method, 0);

    jstring peer_id = env->NewStringUTF(msg.topic_id.ToString().c_str());
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
