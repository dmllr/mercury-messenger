#include "view_model_delegate.hpp"

#include <utility>

#include "class_refs.hpp"
#include "eillance/logging/logging.h"
#include "utils/jni_utils.hpp"

namespace eillance {

AndroidViewModelDelegate::AndroidViewModelDelegate(JavaVM* jvm, jobject native_bridge, std::unique_ptr<view_model::MercuryViewModel> view_model)
    : jvm_(jvm), native_bridge_(AttachCurrentThread()->NewGlobalRef(native_bridge)), view_model_(std::move(view_model)) {
  view_model_->SetOnStateChangedCallback([this](const std::vector<view_model::StateChange>& changes) { NotifyStateChanged(changes); });
}

AndroidViewModelDelegate::~AndroidViewModelDelegate() {
  if (native_bridge_ != nullptr) {
    if (auto* env = AttachCurrentThread()) {
      env->DeleteGlobalRef(native_bridge_);
      native_bridge_ = nullptr;
    }
  }
}

void AndroidViewModelDelegate::NotifyStateChanged(const std::vector<view_model::StateChange>& changes) {
  if (native_bridge_ == nullptr || changes.empty()) { return; }
  auto* env = AttachCurrentThread();
  if (env == nullptr) { return; }

  auto& callback_class_holder = ClassRefs::Instance().Class("hela/eillance/CallService");
  jmethodID callback_method = callback_class_holder.GetMethod("onStateChanged");

  if (callback_method != nullptr) {
    jintArray change_set = env->NewIntArray(static_cast<jsize>(changes.size()));
    if (change_set == nullptr) {
      LOGE << "Failed to allocate jintArray for state changes.";
      return;
    }

    std::vector<jint> int_changes;
    int_changes.reserve(changes.size());
    for (const auto& change: changes) {
      int_changes.push_back(static_cast<jint>(change));
    }

    env->SetIntArrayRegion(change_set, 0, static_cast<jsize>(int_changes.size()), int_changes.data());
    env->CallVoidMethod(native_bridge_, callback_method, change_set);
    env->DeleteLocalRef(change_set);
  }
}

auto AndroidViewModelDelegate::GetMyProfile(JNIEnv* env) const -> jobject {
  auto& class_refs = ClassRefs::Instance();
  auto profile = view_model_->GetMyProfile();

  auto& profile_holder = class_refs.Class("hela/eillance/ndkio/MyProfileInfo");

  jstring my_id = env->NewStringUTF(profile.my_id.ToString().c_str());
  jstring display_name = profile.my_display_name ? env->NewStringUTF(profile.my_display_name->c_str()) : nullptr;
  jstring bio = profile.my_bio ? env->NewStringUTF(profile.my_bio->c_str()) : nullptr;
  jstring photo = profile.my_photo ? env->NewStringUTF(profile.my_photo->c_str()) : nullptr;

  jobject profile_java = profile_holder.NewObject(env, my_id, profile.identity_exists, display_name, bio, photo);

  env->DeleteLocalRef(my_id);
  if (display_name) env->DeleteLocalRef(display_name);
  if (bio) env->DeleteLocalRef(bio);
  if (photo) env->DeleteLocalRef(photo);

  return profile_java;
}

auto AndroidViewModelDelegate::GetContacts(JNIEnv* env) const -> jobject {
  auto& class_refs = ClassRefs::Instance();
  auto contacts = view_model_->GetContacts();

  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject contact_list = list_holder.NewObject(env);
  jmethodID list_add_method = list_holder.GetMethod("add");
  auto& contact_holder = class_refs.Class("hela/eillance/ndkio/Contact");

  for (const auto& contact_cpp: contacts) {
    jstring peer_id = env->NewStringUTF(contact_cpp.peer_id.ToString().c_str());
    jstring name = env->NewStringUTF(contact_cpp.display_name.c_str());
    jstring bio = contact_cpp.bio ? env->NewStringUTF(contact_cpp.bio->c_str()) : nullptr;
    jstring photo = contact_cpp.photo ? env->NewStringUTF(contact_cpp.photo->c_str()) : nullptr;
    jobject contact_java = contact_holder.NewObject(env, peer_id, name, bio, photo,
                                                    static_cast<jint>(contact_cpp.status),
                                                    static_cast<jint>(contact_cpp.connection_state),
                                                    static_cast<jint>(contact_cpp.unread_count));
    env->CallBooleanMethod(contact_list, list_add_method, contact_java);

    env->DeleteLocalRef(peer_id);
    env->DeleteLocalRef(name);
    if (bio) env->DeleteLocalRef(bio);
    if (photo) env->DeleteLocalRef(photo);
    env->DeleteLocalRef(contact_java);
  }
  return contact_list;
}

auto AndroidViewModelDelegate::GetInteractionHistoryPage(JNIEnv* env) const -> jobject {
  auto& class_refs = ClassRefs::Instance();
  auto history = view_model_->GetInteractionHistory();
  bool has_older = view_model_->HasOlderMessages();

  // Create List<HistoryEntry>
  auto& list_holder = class_refs.Class("java/util/ArrayList");
  jobject history_list = list_holder.NewObject(env);
  jmethodID list_add_method = list_holder.GetMethod("add");
  auto& entry_holder = class_refs.Class("hela/eillance/ndkio/HistoryEntry");

  for (const auto& entry_cpp: history) {
    const std::string* content_str = std::visit(
        [](auto&& arg) -> const std::string* {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, messaging::MessageEntry>) { return &arg.content; }
          return nullptr;
        },
        entry_cpp.data);

    const auto* call_entry = std::get_if<messaging::CallEntry>(&entry_cpp.data);

    jstring content = content_str ? env->NewStringUTF(content_str->c_str()) : nullptr;
    jint duration = call_entry ? static_cast<jint>(call_entry->duration_seconds) : 0;

    jobject entry_java = entry_holder.NewObject(env, (jlong)entry_cpp.id, (jint)entry_cpp.kind, entry_cpp.timestamp,
                                                content, (jint)entry_cpp.direction, duration,
                                                call_entry ? (jint)call_entry->call_direction : -1,
                                                call_entry ? (jint)call_entry->call_status : -1, (jint)entry_cpp.status,
                                                (jboolean)entry_cpp.is_truncated);

    env->CallBooleanMethod(history_list, list_add_method, entry_java);

    if (content) { env->DeleteLocalRef(content); }
    env->DeleteLocalRef(entry_java);
  }

  // Create HistoryPage object
  auto& page_holder = class_refs.Class("hela/eillance/ndkio/HistoryPage");
  return page_holder.NewObject(env, history_list, has_older);
}

auto AndroidViewModelDelegate::GetCallState(JNIEnv* env) const -> jobject {
  auto& class_refs = ClassRefs::Instance();
  auto call_state = view_model_->GetCallState();
  auto& call_state_holder = class_refs.Class("hela/eillance/ndkio/CallState");

  jstring peer_id = env->NewStringUTF(call_state.peer_id.ToString().c_str());
  jobject call_state_java = call_state_holder.NewObject(env, call_state.is_connecting, call_state.is_in_call, call_state.is_ringing,
                                                        (jint)call_state.call_direction, peer_id,
                                                        (jint)call_state.call_type);
  env->DeleteLocalRef(peer_id);
  return call_state_java;
}

}  // namespace eillance
