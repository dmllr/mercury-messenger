#pragma once

#include <jni.h>
#include <memory>
#include <vector>

#include "class_refs.hpp"
#include "eillance/view_model/call_view_model.hpp"
#include "eillance/view_model/contacts_view_model.hpp"
#include "eillance/view_model/conversation_view_model.hpp"
#include "eillance/view_model/profile_view_model.hpp"
#include "eillance/view_model/settings_view_model.hpp"
#include "utils/jni_utils.hpp"

namespace eillance {

class AndroidObserverBase {
protected:
  JavaVM* jvm_;
  jobject java_listener_;

  [[nodiscard]] auto AttachCurrentThread() const -> JNIEnv* { return jni::AttachCurrentThreadIfNeeded(jvm_); }

public:
  AndroidObserverBase(JavaVM* jvm, jobject java_listener) : jvm_(jvm) {
    if (auto* env = AttachCurrentThread()) {
      java_listener_ = env->NewGlobalRef(java_listener);
    }
  }

  virtual ~AndroidObserverBase() {
    if (java_listener_) {
      if (auto* env = AttachCurrentThread()) {
        env->DeleteGlobalRef(java_listener_);
      }
    }
  }
};

class AndroidContactsObserver final : public AndroidObserverBase, public view_model::ContactsViewModel::Observer {
public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnContactsUpdated(const std::vector<view_model::ViewContact>& contacts) override {
    auto* env = AttachCurrentThread();
    if (!env || !java_listener_) return;

    auto& class_refs = ClassRefs::Instance();
    auto& list_holder = class_refs.Class("java/util/ArrayList");
    jobject contact_list = list_holder.NewObject(env);
    jmethodID list_add_method = list_holder.GetMethod("add");
    auto& contact_holder = class_refs.Class("hela/eillance/ndkio/Contact");

    for (const auto& contact_cpp: contacts) {
      jstring peer_id = env->NewStringUTF(contact_cpp.peer_id.ToString().c_str());
      jstring name = env->NewStringUTF(contact_cpp.display_name.c_str());
      jstring bio = contact_cpp.bio ? env->NewStringUTF(contact_cpp.bio->c_str()) : nullptr;
      jstring photo = contact_cpp.photo ? env->NewStringUTF(contact_cpp.photo->c_str()) : nullptr;
      jstring snippet = contact_cpp.last_message_snippet ? env->NewStringUTF(contact_cpp.last_message_snippet->c_str()) : nullptr;

      jobject contact_java = contact_holder.NewObject(env, peer_id, name, bio, photo,
                                                      static_cast<jint>(contact_cpp.status),
                                                      static_cast<jint>(contact_cpp.connection_state),
                                                      static_cast<jint>(contact_cpp.unread_count),
                                                      snippet,
                                                      static_cast<jlong>(contact_cpp.last_activity_timestamp));
      env->CallBooleanMethod(contact_list, list_add_method, contact_java);

      env->DeleteLocalRef(peer_id);
      env->DeleteLocalRef(name);
      if (bio) env->DeleteLocalRef(bio);
      if (photo) env->DeleteLocalRef(photo);
      if (snippet) env->DeleteLocalRef(snippet);
      env->DeleteLocalRef(contact_java);
    }

    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeContactsObserver");
    jmethodID on_update = listener_holder.GetMethod("onContactsUpdated");
    env->CallVoidMethod(java_listener_, on_update, contact_list);
    env->DeleteLocalRef(contact_list);
  }
};

class AndroidConversationObserver final : public AndroidObserverBase, public view_model::ConversationViewModel::Observer {
public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnHistoryUpdated(const view_model::ConversationViewModel::State& state) override {
    auto* env = AttachCurrentThread();
    if (!env || !java_listener_) return;

    auto& class_refs = ClassRefs::Instance();
    auto& list_holder = class_refs.Class("java/util/ArrayList");
    jobject history_list = list_holder.NewObject(env);
    jmethodID list_add_method = list_holder.GetMethod("add");
    auto& entry_holder = class_refs.Class("hela/eillance/ndkio/HistoryEntry");

    for (const auto& entry_cpp: state.history) {
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

      jobject entry_java = entry_holder.NewObject(env, (jlong)entry_cpp.id, (jint)entry_cpp.kind, (jlong)entry_cpp.timestamp,
                                                  content, (jint)entry_cpp.direction, duration,
                                                  call_entry ? (jint)call_entry->call_direction : -1,
                                                  call_entry ? (jint)call_entry->call_status : -1, (jint)entry_cpp.status,
                                                  (jboolean)entry_cpp.is_truncated);

      env->CallBooleanMethod(history_list, list_add_method, entry_java);
      if (content) env->DeleteLocalRef(content);
      env->DeleteLocalRef(entry_java);
    }

    auto& page_holder = class_refs.Class("hela/eillance/ndkio/HistoryPage");
    jobject page_java = page_holder.NewObject(env, history_list, state.has_older_messages);

    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeConversationObserver");
    jmethodID on_update = listener_holder.GetMethod("onHistoryUpdated");
    env->CallVoidMethod(java_listener_, on_update, page_java);

    env->DeleteLocalRef(history_list);
    env->DeleteLocalRef(page_java);
  }
};

class AndroidProfileObserver final : public AndroidObserverBase, public view_model::ProfileViewModel::Observer {
public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnProfileUpdated(const view_model::MyProfile& profile) override {
    auto* env = AttachCurrentThread();
    if (!env || !java_listener_) return;

    auto& class_refs = ClassRefs::Instance();
    auto& profile_holder = class_refs.Class("hela/eillance/ndkio/MyProfileInfo");

    jstring my_id = env->NewStringUTF(profile.my_id.ToString().c_str());
    jstring display_name = profile.my_display_name ? env->NewStringUTF(profile.my_display_name->c_str()) : nullptr;
    jstring bio = profile.my_bio ? env->NewStringUTF(profile.my_bio->c_str()) : nullptr;
    jstring photo = profile.my_photo ? env->NewStringUTF(profile.my_photo->c_str()) : nullptr;

    jobject profile_java = profile_holder.NewObject(env, my_id, profile.identity_exists, display_name, bio, photo);

    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeProfileObserver");
    jmethodID on_update = listener_holder.GetMethod("onProfileUpdated");
    env->CallVoidMethod(java_listener_, on_update, profile_java);

    env->DeleteLocalRef(my_id);
    if (display_name) env->DeleteLocalRef(display_name);
    if (bio) env->DeleteLocalRef(bio);
    if (photo) env->DeleteLocalRef(photo);
    env->DeleteLocalRef(profile_java);
  }
};

class AndroidCallObserver final : public AndroidObserverBase, public view_model::CallViewModel::Observer {
public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnCallStateChanged(const view_model::CallState& call_state) override {
    auto* env = AttachCurrentThread();
    if (!env || !java_listener_) return;

    auto& class_refs = ClassRefs::Instance();
    auto& call_state_holder = class_refs.Class("hela/eillance/ndkio/CallState");

    jstring peer_id = env->NewStringUTF(call_state.peer_id.ToString().c_str());
    jobject call_state_java = call_state_holder.NewObject(env, call_state.is_connecting, call_state.is_in_call, call_state.is_ringing,
                                                          (jint)call_state.call_direction, peer_id,
                                                          (jint)call_state.call_type);

    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeCallObserver");
    jmethodID on_update = listener_holder.GetMethod("onCallStateChanged");
    env->CallVoidMethod(java_listener_, on_update, call_state_java);

    env->DeleteLocalRef(peer_id);
    env->DeleteLocalRef(call_state_java);
  }
};

class AndroidSettingsObserver final : public AndroidObserverBase, public view_model::SettingsViewModel::Observer {
public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnSettingsUpdated(const std::optional<std::string>& signaling_url, int timeout) override {
    auto* env = AttachCurrentThread();
    if (!env || !java_listener_) return;

    jstring url_str = signaling_url ? env->NewStringUTF(signaling_url->c_str()) : nullptr;

    auto& class_refs = ClassRefs::Instance();
    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeSettingsObserver");
    jmethodID on_update = listener_holder.GetMethod("onSettingsUpdated");
    env->CallVoidMethod(java_listener_, on_update, url_str, (jint)timeout);

    if (url_str) env->DeleteLocalRef(url_str);
  }

  void OnConnectionStateChanged(ServiceConnectionState state) override {
    auto* env = AttachCurrentThread();
    if (!env || !java_listener_) return;

    auto& class_refs = ClassRefs::Instance();
    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeSettingsObserver");
    jmethodID on_update = listener_holder.GetMethod("onConnectionStateChanged");
    env->CallVoidMethod(java_listener_, on_update, (jint)state);
  }
};

}  // namespace eillance
