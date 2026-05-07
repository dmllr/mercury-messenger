#include <vector>

#include "bridge_context.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/utils/event_queue.hpp"
#include "utils/class_refs.hpp"
#include "utils/jni_utils.hpp"

using namespace eillance;

std::vector<EventQueue::SubscriberTypeId> g_eventBridgeUnsubs;

void UnsubscribeAllEvents() {
  for (auto& evnt : g_eventBridgeUnsubs) {
    EventQueue::Instance().Unsubscribe(evnt);
  }
  g_eventBridgeUnsubs.clear();
}

void InitEventBridge(JavaVM* jvm) {
  auto& q = EventQueue::Instance();  // NOLINT(*-identifier-length)

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventContactsStateUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv const env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& list_holder = class_refs.Class("java/util/ArrayList");
      jobject contact_list = list_holder.NewObject(env);
      jmethodID list_add_method = list_holder.GetMethod("add");
      auto& contact_holder = class_refs.Class("hela/eillance/ndkio/Contact");

      for (auto const& contact_cpp : e.contacts) {
        jstring peer_id = env->NewStringUTF(contact_cpp.peer_id.ToString().c_str());
        jstring name = env->NewStringUTF(contact_cpp.display_name.c_str());
        jstring bio = contact_cpp.bio ? env->NewStringUTF(contact_cpp.bio->c_str()) : nullptr;
        jstring photo = contact_cpp.photo ? env->NewStringUTF(contact_cpp.photo->c_str()) : nullptr;

        jint lm_kind = -1;
        jstring lm_content = nullptr;
        jint lm_direction = -1;
        jint lm_status = 0;
        jstring lm_att_path = nullptr;
        jint lm_att_type = -1;
        jint lm_duration = 0;

        if (contact_cpp.last_message) {
          lm_kind = static_cast<jint>(contact_cpp.last_message->kind);
          lm_direction = static_cast<jint>(contact_cpp.last_message->direction);
          lm_status = static_cast<jint>(contact_cpp.last_message->status);

          if (std::holds_alternative<messaging::MessageEntry>(contact_cpp.last_message->data)) {
            auto const& txt = std::get<messaging::MessageEntry>(contact_cpp.last_message->data);
            lm_content = env->NewStringUTF(txt.content.c_str());
            if (!txt.attachments.empty()) {
              lm_att_path = env->NewStringUTF(txt.attachments.front().file_path.c_str());
              lm_att_type = static_cast<jint>(txt.attachments.front().type);
            }
          }
          auto const* call = std::get_if<messaging::CallEntry>(&contact_cpp.last_message->data);
          if (call) lm_duration = static_cast<jint>(call->duration_seconds);
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
            lm_status,
            static_cast<jlong>(contact_cpp.last_activity_timestamp),
            lm_att_path,
            lm_att_type,
            lm_duration
        );
        env->CallBooleanMethod(contact_list, list_add_method, contact_java);

        env->DeleteLocalRef(peer_id);
        env->DeleteLocalRef(name);
        if (bio) env->DeleteLocalRef(bio);
        if (photo) env->DeleteLocalRef(photo);
        if (lm_content) env->DeleteLocalRef(lm_content);
        if (lm_att_path) env->DeleteLocalRef(lm_att_path);
        env->DeleteLocalRef(contact_java);
      }

      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onContactsUpdated"), contact_list);
      env->DeleteLocalRef(contact_list);
    }
  }));
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventConversationStateUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& list_holder = class_refs.Class("java/util/ArrayList");
      jobject history_list = list_holder.NewObject(env);
      jmethodID list_add_method = list_holder.GetMethod("add");
      auto& entry_holder = class_refs.Class("hela/eillance/ndkio/HistoryEntry");
      auto& attachment_holder = class_refs.Class("hela/eillance/ndkio/Attachment");
      auto& reaction_holder = class_refs.Class("hela/eillance/ndkio/ReactionEntry");

      for (auto const& entry_cpp : e.state.history) {
        std::string const* content_str = nullptr;
        std::vector<messaging::Attachment> const* attachments_ptr = nullptr;
        jlong j_reply_to_id = 0;
        std::visit(
            [&](auto&& arg) {
              using T = std::decay_t<decltype(arg)>;
              if constexpr (std::is_same_v<T, messaging::MessageEntry>) {
                content_str = &arg.content;
                attachments_ptr = &arg.attachments;
                j_reply_to_id = arg.reply_to_id;
              }
            },
            entry_cpp.data
        );
        auto const* call_entry = std::get_if<messaging::CallEntry>(&entry_cpp.data);
        jstring content = content_str ? env->NewStringUTF(content_str->c_str()) : nullptr;
        jint const duration = call_entry ? static_cast<jint>(call_entry->duration_seconds) : 0;

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
            if (j_thumb) env->DeleteLocalRef(j_thumb);
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
        if (content) env->DeleteLocalRef(content);
        env->DeleteLocalRef(j_attachments);
        env->DeleteLocalRef(j_reactions);
        env->DeleteLocalRef(entry_java);
      }
      auto& page_holder = class_refs.Class("hela/eillance/ndkio/HistoryPage");
      jobject page_java =
          page_holder
              .NewObject(env, history_list, e.state.has_older_messages, static_cast<jboolean>(e.state.is_unreachable));
      jstring j_peer_id = env->NewStringUTF(e.peer_id.ToString().c_str());
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onHistoryUpdated"), j_peer_id, page_java);
      env->DeleteLocalRef(history_list);
      env->DeleteLocalRef(page_java);
      env->DeleteLocalRef(j_peer_id);
    }
  }));

  g_eventBridgeUnsubs.emplace_back(
      q.Subscribe<view_model::EventConversationDraftsUpdated>([jvm](auto const& e) -> void {
        jni::ScopedJniEnv env_scope(jvm);
        if (auto* env = env_scope.Get()) {
          auto& class_refs = ClassRefs::Instance();
          auto& list_holder = class_refs.Class("java/util/ArrayList");
          jobject drafts_list = list_holder.NewObject(env);
          jmethodID list_add_method = list_holder.GetMethod("add");
          auto& draft_holder = class_refs.Class("hela/eillance/ndkio/DraftAttachment");

          for (auto const& draft : e.drafts) {
            jstring path = env->NewStringUTF(draft.file_path.c_str());
            jint type = static_cast<jint>(draft.type);
            jstring mime = env->NewStringUTF(draft.mime_type.c_str());
            jstring name = env->NewStringUTF(draft.file_name.c_str());
            jstring thumb = env->NewStringUTF(draft.thumbnail_base64.c_str());
            jstring note = env->NewStringUTF(draft.note.c_str());
            jobject draft_java = draft_holder.NewObject(env, path, type, mime, name, thumb, note);
            env->CallBooleanMethod(drafts_list, list_add_method, draft_java);
            env->DeleteLocalRef(path);
            env->DeleteLocalRef(mime);
            env->DeleteLocalRef(name);
            env->DeleteLocalRef(thumb);
            env->DeleteLocalRef(note);
            env->DeleteLocalRef(draft_java);
          }

          jstring j_peer_id = env->NewStringUTF(e.peer_id.ToString().c_str());
          auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
          env->CallStaticVoidMethod(
              bus_holder.Class(),
              bus_holder.GetMethod("onDraftsUpdated"),
              j_peer_id,
              drafts_list
          );
          env->DeleteLocalRef(drafts_list);
          env->DeleteLocalRef(j_peer_id);
        }
      })
  );

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventTransferProgressUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      jstring j_att = env->NewStringUTF(e.attachment_id.c_str());
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onTransferProgress"),
          j_peer,
          j_att,
          e.progress
      );
      env->DeleteLocalRef(j_peer);
      env->DeleteLocalRef(j_att);
    }
  }));
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventProfileStateUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& profile_holder = class_refs.Class("hela/eillance/ndkio/MyProfileInfo");
      jstring my_id = env->NewStringUTF(e.profile.my_id.ToString().c_str());
      jstring display_name =
          e.profile.my_display_name ? env->NewStringUTF(e.profile.my_display_name->c_str()) : nullptr;
      jstring bio = e.profile.my_bio ? env->NewStringUTF(e.profile.my_bio->c_str()) : nullptr;
      jstring photo = e.profile.my_photo ? env->NewStringUTF(e.profile.my_photo->c_str()) : nullptr;
      jobject profile_java = profile_holder.NewObject(env, my_id, e.profile.identity_exists, display_name, bio, photo);
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onProfileUpdated"), profile_java);
      env->DeleteLocalRef(my_id);
      if (display_name) env->DeleteLocalRef(display_name);
      if (bio) env->DeleteLocalRef(bio);
      if (photo) env->DeleteLocalRef(photo);
      env->DeleteLocalRef(profile_java);
    }
  }));
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventCallStateChanged>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& call_state_holder = class_refs.Class("hela/eillance/ndkio/CallState");
      jstring peer_id = env->NewStringUTF(e.state.peer_id.ToString().c_str());
      jobject call_state_java = call_state_holder.NewObject(
          env,
          e.state.is_connecting,
          e.state.is_in_call,
          e.state.is_ringing,
          (jint)e.state.call_direction,
          peer_id,
          (jint)e.state.call_type,
          e.state.is_muted,
          e.state.is_speaker_on,
          (jint)e.state.duration_seconds
      );
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onCallStateChanged"), call_state_java);
      env->DeleteLocalRef(peer_id);
      env->DeleteLocalRef(call_state_java);
    }
  }));
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventSettingsStateUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      jstring url_str = e.state.bootstrap_node_url ? env->NewStringUTF(e.state.bootstrap_node_url->c_str()) : nullptr;
      jstring stun_str = e.state.custom_stun ? env->NewStringUTF(e.state.custom_stun->c_str()) : nullptr;
      jstring turn_str = e.state.custom_turn ? env->NewStringUTF(e.state.custom_turn->c_str()) : nullptr;
      jstring user_str = e.state.custom_turn_user ? env->NewStringUTF(e.state.custom_turn_user->c_str()) : nullptr;
      jstring pass_str = e.state.custom_turn_pass ? env->NewStringUTF(e.state.custom_turn_pass->c_str()) : nullptr;
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onSettingsUpdated"),
          url_str,
          static_cast<jint>(e.state.peer_inactivity_timeout_seconds),
          stun_str,
          turn_str,
          user_str,
          pass_str,
          static_cast<jboolean>(e.state.play_sound_on_send),
          static_cast<jboolean>(e.state.play_sound_on_receive),
          static_cast<jboolean>(e.state.show_notification_content)
      );
      if (url_str) env->DeleteLocalRef(url_str);
      if (stun_str) env->DeleteLocalRef(stun_str);
      if (turn_str) env->DeleteLocalRef(turn_str);
      if (user_str) env->DeleteLocalRef(user_str);
      if (pass_str) env->DeleteLocalRef(pass_str);
    }
  }));
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventServiceConnectionStateChanged>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onConnectionStateChanged"),
          static_cast<jint>(e.state)
      );
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventPermissionBannerChanged>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onPermissionBannerChanged"),
          static_cast<jint>(e.type)
      );
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventNetworkStatsUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");

      jstring j_role = env->NewStringUTF(e.device_role.c_str());
      jmethodID mid = bus_holder.GetMethod("onNetworkStatsUpdated");
      if (mid != nullptr) {
        env->CallStaticVoidMethod(
            bus_holder.Class(),
            mid,
            e.connected_peers,
            e.bootstrap_nodes_alive,
            e.dht_peers,
            j_role
        );
      }
      env->DeleteLocalRef(j_role);
    }
  }));

  // Setup explicitly requested notification triggers
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventNotifyNewMessage>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv const env_scope(jvm);
    auto* env = env_scope.Get();
    if (env == nullptr) {
      return;
    }
    auto& class_refs = ClassRefs::Instance();
    auto& list_holder = class_refs.Class("java/util/ArrayList");
    jobject history_list = list_holder.NewObject(env);
    jmethodID list_add_method = list_holder.GetMethod("add");
    auto& entry_holder = class_refs.Class("hela/eillance/ndkio/HistoryEntry");
    auto& attachment_holder = class_refs.Class("hela/eillance/ndkio/Attachment");
    auto& reaction_holder = class_refs.Class("hela/eillance/ndkio/ReactionEntry");

    for (auto const& entry_cpp : e.unread_messages) {
      std::string const* content_str = nullptr;
      std::vector<messaging::Attachment> const* attachments_ptr = nullptr;
      jlong j_reply_to_id = 0;
      if (std::holds_alternative<messaging::MessageEntry>(entry_cpp.data)) {
        auto const& msg = std::get<messaging::MessageEntry>(entry_cpp.data);
        content_str = &msg.content;
        attachments_ptr = &msg.attachments;
        j_reply_to_id = msg.reply_to_id;
      }
      auto const* call_entry = std::get_if<messaging::CallEntry>(&entry_cpp.data);
      jstring content = content_str ? env->NewStringUTF(content_str->c_str()) : nullptr;
      jint const duration = call_entry ? static_cast<jint>(call_entry->duration_seconds) : 0;

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
          if (j_thumb) env->DeleteLocalRef(j_thumb);
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
      jobject entry_java = entry_holder.NewObject(
          env,
          static_cast<jlong>(entry_cpp.id),
          static_cast<jint>(entry_cpp.kind),
          static_cast<jlong>(entry_cpp.timestamp),
          content,
          static_cast<jint>(entry_cpp.direction),
          duration,
          call_entry ? static_cast<jint>(call_entry->call_direction) : -1,
          call_entry ? static_cast<jint>(call_entry->call_status) : -1,
          static_cast<jint>(entry_cpp.status),
          static_cast<jboolean>(entry_cpp.is_truncated),
          static_cast<jboolean>(entry_cpp.is_edited),
          static_cast<jboolean>(entry_cpp.is_deleted),
          j_attachments,
          j_reactions,
          j_reply_to_id
      );
      env->CallBooleanMethod(history_list, list_add_method, entry_java);
      if (content) env->DeleteLocalRef(content);
      env->DeleteLocalRef(j_attachments);
      env->DeleteLocalRef(j_reactions);
      env->DeleteLocalRef(entry_java);
    }

    auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
    jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
    jstring j_name = env->NewStringUTF(e.contact_name.c_str());
    env->CallStaticVoidMethod(
        bus_holder.Class(),
        bus_holder.GetMethod("onNotifyNewMessage"),
        j_peer,
        j_name,
        history_list
    );

    env->DeleteLocalRef(j_peer);
    env->DeleteLocalRef(j_name);
    env->DeleteLocalRef(history_list);
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventNotifyMissedCall>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      jstring j_name = env->NewStringUTF(e.contact_name.c_str());
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onNotifyMissedCall"), j_peer, j_name);
      env->DeleteLocalRef(j_peer);
      env->DeleteLocalRef(j_name);
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventNotifyContactRequest>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      jstring j_name = env->NewStringUTF(e.contact_name.c_str());
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onNotifyContactRequest"), j_peer, j_name);
      env->DeleteLocalRef(j_peer);
      env->DeleteLocalRef(j_name);
    }
  }));

  // Hardware / OS Orchestration Commands
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdProcessIncomingTelecomCall>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onCmdProcessIncomingTelecom"), j_peer);
      env->DeleteLocalRef(j_peer);
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdProcessOutgoingTelecomCall>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onCmdProcessOutgoingTelecom"),
          j_peer,
          static_cast<jboolean>(e.is_video)
      );
      env->DeleteLocalRef(j_peer);
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdDisconnectTelecomCall>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onCmdDisconnectTelecom"), j_peer);
      env->DeleteLocalRef(j_peer);
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdSetTelecomCallActive>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onCmdSetTelecomActive"), j_peer);
      env->DeleteLocalRef(j_peer);
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdSetAudioMode>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onCmdSetAudioMode"),
          static_cast<jint>(e.mode)
      );
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdClearCommunicationDevice>([jvm](auto const&) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onCmdClearCommunicationDevice"));
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdPlaySound>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onCmdPlaySound"), static_cast<jint>(e.type));
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdStopSound>([jvm](auto const&) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(bus_holder.Class(), bus_holder.GetMethod("onCmdStopSound"));
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdSetMicrophoneMute>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onCmdSetMicrophoneMute"),
          static_cast<jboolean>(e.is_muted)
      );
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventCmdSetSpeakerphone>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onCmdSetSpeakerphone"),
          static_cast<jboolean>(e.is_speaker_on)
      );
    }
  }));

  g_eventBridgeUnsubs.emplace_back(q.Subscribe<EventPeerVersionMismatch>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.peer_id.ToString().c_str());
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onPeerVersionMismatch"),
          j_peer,
          static_cast<jboolean>(e.we_are_outdated)
      );
      env->DeleteLocalRef(j_peer);
    }
  }));
}
