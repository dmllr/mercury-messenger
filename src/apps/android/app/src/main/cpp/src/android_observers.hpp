#pragma once

#include <memory>
#include <vector>

#include <jni.h>

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

  [[nodiscard]] auto GetEnv() const -> jni::ScopedJniEnv {
    return jni::ScopedJniEnv(jvm_);
  }

 public:
  AndroidObserverBase(JavaVM* jvm, jobject java_listener)
      : jvm_(jvm) {
    auto env_scope = GetEnv();
    if (auto* env = env_scope.Get()) {
      java_listener_ = env->NewGlobalRef(java_listener);
    }
  }

  virtual ~AndroidObserverBase() {
    if (java_listener_ != nullptr) {
      auto env_scope = GetEnv();
      if (auto* env = env_scope.Get()) {
        env->DeleteGlobalRef(java_listener_);
      }
    }
  }
};

class AndroidContactsObserver final
    : public AndroidObserverBase
    , public view_model::ContactsViewModel::Observer {
 public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnContactsUpdated(std::vector<view_model::ViewContact> const& contacts) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (java_listener_ == nullptr)) {
      return;
    }

    auto& class_refs = ClassRefs::Instance();
    auto& list_holder = class_refs.Class("java/util/ArrayList");
    jobject contact_list = list_holder.NewObject(env);
    jmethodID list_add_method = list_holder.GetMethod("add");
    auto& contact_holder = class_refs.Class("hela/eillance/ndkio/Contact");

    for (auto const& contact_cpp : contacts) {
      jstring peer_id = env->NewStringUTF(contact_cpp.peer_id.ToString().c_str());
      jstring name = env->NewStringUTF(contact_cpp.display_name.c_str());
      jstring bio = contact_cpp.bio ? env->NewStringUTF(contact_cpp.bio->c_str()) : nullptr;
      jstring photo = contact_cpp.photo ? env->NewStringUTF(contact_cpp.photo->c_str()) : nullptr;

      jint lm_kind = -1;
      jstring lm_content = nullptr;
      jint lm_direction = -1;

      if (contact_cpp.last_message) {
        lm_kind = static_cast<jint>(contact_cpp.last_message->kind);
        lm_direction = static_cast<jint>(contact_cpp.last_message->direction);

        if (std::holds_alternative<messaging::MessageEntry>(contact_cpp.last_message->data)) {
          auto const& txt = std::get<messaging::MessageEntry>(contact_cpp.last_message->data);
          lm_content = env->NewStringUTF(txt.content.c_str());
        }
      }

      jobject contact_java = contact_holder.NewObject(
          env,
          peer_id,
          name,
          bio,
          photo,
          static_cast<jint>(contact_cpp.status),
          static_cast<jint>(contact_cpp.connection_state),
          static_cast<jint>(contact_cpp.unread_count),
          lm_kind,
          lm_content,
          lm_direction,
          static_cast<jlong>(contact_cpp.last_activity_timestamp)
      );
      env->CallBooleanMethod(contact_list, list_add_method, contact_java);

      env->DeleteLocalRef(peer_id);
      env->DeleteLocalRef(name);
      if (bio != nullptr) {
        env->DeleteLocalRef(bio);
      }
      if (photo != nullptr) {
        env->DeleteLocalRef(photo);
      }
      if (lm_content != nullptr) {
        env->DeleteLocalRef(lm_content);
      }
      env->DeleteLocalRef(contact_java);
    }

    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeContactsObserver");
    jmethodID on_update = listener_holder.GetMethod("onContactsUpdated");
    env->CallVoidMethod(java_listener_, on_update, contact_list);
    env->DeleteLocalRef(contact_list);
  }
};

class AndroidConversationObserver final
    : public AndroidObserverBase
    , public view_model::ConversationViewModel::Observer {
 public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnHistoryUpdated(view_model::ConversationViewModel::State const& state) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (java_listener_ == nullptr)) {
      return;
    }

    auto& class_refs = ClassRefs::Instance();
    auto& list_holder = class_refs.Class("java/util/ArrayList");
    jobject history_list = list_holder.NewObject(env);
    jmethodID list_add_method = list_holder.GetMethod("add");
    auto& entry_holder = class_refs.Class("hela/eillance/ndkio/HistoryEntry");
    auto& attachment_holder = class_refs.Class("hela/eillance/ndkio/Attachment");

    for (auto const& entry_cpp : state.history) {
      std::string const* content_str = nullptr;
      std::vector<messaging::Attachment> const* attachments_ptr = nullptr;

      std::visit(
          [&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, messaging::MessageEntry>) {
              content_str = &arg.content;
              attachments_ptr = &arg.attachments;
            }
          },
          entry_cpp.data
      );

      auto const* call_entry = std::get_if<messaging::CallEntry>(&entry_cpp.data);

      jstring content = content_str ? env->NewStringUTF(content_str->c_str()) : nullptr;
      jint duration = call_entry ? static_cast<jint>(call_entry->duration_seconds) : 0;

      // Handle Attachments
      jobject j_attachments = list_holder.NewObject(env);
      if (attachments_ptr && !attachments_ptr->empty()) {
        for (auto const& att : *attachments_ptr) {
          jstring j_id = env->NewStringUTF(att.id.c_str());
          jstring j_path = env->NewStringUTF(att.file_path.c_str());
          jstring j_mime = env->NewStringUTF(att.mime_type.c_str());
          jstring j_thumb = att.thumbnail_base64.empty() ? nullptr : env->NewStringUTF(att.thumbnail_base64.c_str());
          // NOTE must be non-null as per Kotlin definition. NewStringUTF("") creates valid empty string object.
          jstring j_note = env->NewStringUTF(att.note.c_str());

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
              j_note
          );

          env->CallBooleanMethod(j_attachments, list_add_method, j_att);

          env->DeleteLocalRef(j_id);
          env->DeleteLocalRef(j_path);
          env->DeleteLocalRef(j_mime);
          if (j_thumb) env->DeleteLocalRef(j_thumb);
          env->DeleteLocalRef(j_note);
          env->DeleteLocalRef(j_att);
        }
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
          j_attachments
      );

      env->CallBooleanMethod(history_list, list_add_method, entry_java);
      if (content != nullptr) {
        env->DeleteLocalRef(content);
      }
      env->DeleteLocalRef(j_attachments);
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

  void OnTransferProgress(std::string const& attachment_id, float progress) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (java_listener_ == nullptr)) {
      return;
    }

    jstring j_att_id = env->NewStringUTF(attachment_id.c_str());
    auto& class_refs = ClassRefs::Instance();
    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeConversationObserver");
    jmethodID on_progress = listener_holder.GetMethod("onTransferProgress");
    env->CallVoidMethod(java_listener_, on_progress, j_att_id, progress);
    env->DeleteLocalRef(j_att_id);
  }
};

class AndroidProfileObserver final
    : public AndroidObserverBase
    , public view_model::ProfileViewModel::Observer {
 public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnProfileUpdated(view_model::MyProfile const& profile) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (java_listener_ == nullptr)) {
      return;
    }

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

class AndroidCallObserver final
    : public AndroidObserverBase
    , public view_model::CallViewModel::Observer {
 public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnCallStateChanged(view_model::CallState const& call_state) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (java_listener_ == nullptr)) {
      return;
    }

    auto& class_refs = ClassRefs::Instance();
    auto& call_state_holder = class_refs.Class("hela/eillance/ndkio/CallState");

    jstring peer_id = env->NewStringUTF(call_state.peer_id.ToString().c_str());
    jobject call_state_java = call_state_holder.NewObject(
        env,
        call_state.is_connecting,
        call_state.is_in_call,
        call_state.is_ringing,
        (jint)call_state.call_direction,
        peer_id,
        (jint)call_state.call_type
    );

    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeCallObserver");
    jmethodID on_update = listener_holder.GetMethod("onCallStateChanged");
    env->CallVoidMethod(java_listener_, on_update, call_state_java);

    env->DeleteLocalRef(peer_id);
    env->DeleteLocalRef(call_state_java);
  }
};

class AndroidSettingsObserver final
    : public AndroidObserverBase
    , public view_model::SettingsViewModel::Observer {
 public:
  using AndroidObserverBase::AndroidObserverBase;

  void OnSettingsUpdated(std::optional<std::string> const& signaling_url, int timeout) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (java_listener_ == nullptr)) {
      return;
    }

    jstring url_str = signaling_url ? env->NewStringUTF(signaling_url->c_str()) : nullptr;

    auto& class_refs = ClassRefs::Instance();
    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeSettingsObserver");
    jmethodID on_update = listener_holder.GetMethod("onSettingsUpdated");
    env->CallVoidMethod(java_listener_, on_update, url_str, static_cast<jint>(timeout));

    if (url_str != nullptr) {
      env->DeleteLocalRef(url_str);
    }
  }

  void OnConnectionStateChanged(ServiceConnectionState state) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (java_listener_ == nullptr)) {
      return;
    }

    auto& class_refs = ClassRefs::Instance();
    auto& listener_holder = class_refs.Class("hela/eillance/viewmodels/NativeSettingsObserver");
    jmethodID on_update = listener_holder.GetMethod("onConnectionStateChanged");
    env->CallVoidMethod(java_listener_, on_update, static_cast<jint>(state));
  }
};

}  // namespace eillance
