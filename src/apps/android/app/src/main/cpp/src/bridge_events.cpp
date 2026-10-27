#include <vector>

#include "bridge_context.hpp"
#include "bridge_conversions.hpp"
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

static void SubscribeUIEvents(JavaVM* jvm, EventQueue& q) {
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventContactsStateUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv const env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& list_holder = class_refs.Class("java/util/ArrayList");
      jobject contact_list = list_holder.NewObject(env);
      jmethodID list_add_method = list_holder.GetMethod("add");
      auto& contact_holder = class_refs.Class("hela/eillance/ndkio/Contact");

      for (auto const& contact_cpp : e.contacts) {
        jstring peer_id = env->NewStringUTF(contact_cpp.topic_id.ToString().c_str());
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
        jstring owner_id = contact_cpp.owner_id ? env->NewStringUTF(contact_cpp.owner_id->ToString().c_str()) : nullptr;

        if (contact_cpp.last_message) {
          lm_kind = static_cast<jint>(contact_cpp.last_message->kind);
          lm_direction = static_cast<jint>(contact_cpp.last_message->direction);
          lm_status = static_cast<jint>(contact_cpp.last_message->status);

          if (auto const* content = messaging::GetMessageContent(contact_cpp.last_message->data)) {
            lm_content = env->NewStringUTF(content->content.c_str());
            if (!content->attachments.empty()) {
              lm_att_path = env->NewStringUTF(content->attachments.front().file_path.c_str());
              lm_att_type = static_cast<jint>(content->attachments.front().type);
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
            lm_duration,
            static_cast<jboolean>(contact_cpp.is_group),
            static_cast<jboolean>(contact_cpp.is_channel),
            static_cast<jboolean>(contact_cpp.is_read_only),
            owner_id
        );
        env->CallBooleanMethod(contact_list, list_add_method, contact_java);

        env->DeleteLocalRef(peer_id);
        env->DeleteLocalRef(name);
        if (bio) env->DeleteLocalRef(bio);
        if (photo) env->DeleteLocalRef(photo);
        if (lm_content) env->DeleteLocalRef(lm_content);
        if (lm_att_path) env->DeleteLocalRef(lm_att_path);
        if (owner_id) env->DeleteLocalRef(owner_id);
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
      jobject history_list = eillance::jni::ConvertHistoryEntries(env, e.state.history);
      auto& page_holder = class_refs.Class("hela/eillance/ndkio/HistoryPage");
      jobject page_java = page_holder.NewObject(
          env,
          history_list,
          e.state.has_older_messages,
          static_cast<jboolean>(e.state.is_unreachable),
          static_cast<jboolean>(e.state.is_unwilling),
          static_cast<jboolean>(e.state.is_syncing),
          e.state.sync_progress
      );
      jstring j_peer_id = env->NewStringUTF(e.topic_id.ToString().c_str());
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

          jstring j_peer_id = env->NewStringUTF(e.topic_id.ToString().c_str());
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
}

static void SubscribeNotificationEvents(JavaVM* jvm, EventQueue& q) {
  g_eventBridgeUnsubs.emplace_back(q.Subscribe<view_model::EventTransferProgressUpdated>([jvm](auto const& e) -> void {
    jni::ScopedJniEnv env_scope(jvm);
    if (auto* env = env_scope.Get()) {
      auto& class_refs = ClassRefs::Instance();
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      jstring j_peer = env->NewStringUTF(e.topic_id.ToString().c_str());
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
      jstring url_str = e.state.bootstrap_node_urls ? env->NewStringUTF(e.state.bootstrap_node_urls->c_str()) : nullptr;
      jstring turn_str = e.state.custom_turn ? env->NewStringUTF(e.state.custom_turn->c_str()) : nullptr;
      jstring user_str = e.state.custom_turn_user ? env->NewStringUTF(e.state.custom_turn_user->c_str()) : nullptr;
      jstring pass_str = e.state.custom_turn_pass ? env->NewStringUTF(e.state.custom_turn_pass->c_str()) : nullptr;
      auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
      env->CallStaticVoidMethod(
          bus_holder.Class(),
          bus_holder.GetMethod("onSettingsUpdated"),
          url_str,
          static_cast<jboolean>(e.state.append_bootstrap_nodes),
          static_cast<jint>(e.state.peer_inactivity_timeout_seconds),
          turn_str,
          user_str,
          pass_str,
          static_cast<jboolean>(e.state.use_custom_turn_only),
          static_cast<jboolean>(e.state.play_sound_on_send),
          static_cast<jboolean>(e.state.play_sound_on_receive),
          static_cast<jboolean>(e.state.show_notification_content)
      );
      if (url_str) env->DeleteLocalRef(url_str);
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
    jobject history_list = eillance::jni::ConvertHistoryEntries(env, e.unread_messages);

    auto& bus_holder = class_refs.Class("hela/eillance/events/NativeEventBus");
    jstring j_peer = env->NewStringUTF(e.topic_id.ToString().c_str());
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
}

static void SubscribeHardwareEvents(JavaVM* jvm, EventQueue& q) {
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

void InitEventBridge(JavaVM* jvm) {
  auto& q = EventQueue::Instance();
  SubscribeUIEvents(jvm, q);
  SubscribeNotificationEvents(jvm, q);
  SubscribeHardwareEvents(jvm, q);
}
