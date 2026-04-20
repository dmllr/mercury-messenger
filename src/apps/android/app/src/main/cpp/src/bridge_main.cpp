#include <string>

#include <jni.h>

#include "bridge_context.hpp"
#include "eillance/logging/logging.hpp"
#include "utils/class_refs.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern void InitEventBridge(JavaVM* jvm);
extern void UnsubscribeAllEvents();

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_initEngine(JNIEnv* env, jobject instance) {
  if (g_clientEngine == nullptr) {
    g_clientEngine = std::make_unique<eillance::ClientEngine>(g_javaVm, env, instance);
    InitEventBridge(g_javaVm);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_start(JNIEnv* /*env*/, jobject /*instance*/) {
  if (g_clientEngine) {
    g_clientEngine->Start();
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_terminate(JNIEnv* /*env*/, jobject /*instance*/) {
  if (g_clientEngine) {
    g_clientEngine->Terminate();
    g_clientEngine.reset();
    UnsubscribeAllEvents();
  }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_hela_eillance_MessengerService_identityExists(JNIEnv* /*env*/, jobject /*instance*/) {
  if (g_clientEngine) {
    return g_clientEngine->GetClient()->IdentityExists();
  }
  return false;
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_MessengerService_configureVideoCapture(JNIEnv* env, jobject /*instance*/, jobject video_config) {
  if (g_clientEngine) {
    auto& video_config_holder = ClassRefs::Instance().Class("hela/eillance/ndkio/VideoConfig");
    auto conf = eillance::VideoConfig{};
    conf.camera_id = env->GetIntField(video_config, video_config_holder.GetField("cameraId"));
    conf.width = env->GetIntField(video_config, video_config_holder.GetField("width"));
    conf.height = env->GetIntField(video_config, video_config_holder.GetField("height"));
    conf.frame_rate = env->GetIntField(video_config, video_config_holder.GetField("frameRate"));
    conf.bitrate = env->GetIntField(video_config, video_config_holder.GetField("bitrate"));
    g_clientEngine->ConfigureVideoCapture(conf);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeRestart(JNIEnv* env, jobject instance) {
  if (g_clientEngine) {
    g_clientEngine->Terminate();
    g_clientEngine.reset();
    UnsubscribeAllEvents();
  }
  g_clientEngine = std::make_unique<eillance::ClientEngine>(g_javaVm, env, instance);
  InitEventBridge(g_javaVm);
  g_clientEngine->Start();
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_MessengerService_nativeOnAppForeground(JNIEnv* /*env*/, jobject /*instance*/) {
  eillance::EventQueue::Instance().Push(eillance::EventAppForegrounded{});
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_MessengerService_nativeOnAppBackground(JNIEnv* /*env*/, jobject /*instance*/) {
  eillance::EventQueue::Instance().Push(eillance::EventAppBackgrounded{});
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeAcceptCall(JNIEnv*, jobject) {
  if (g_clientEngine) {
    auto state = g_clientEngine->GetClient()->GetCurrentCallState();
    if (!state.peer_id.IsEmpty()) g_clientEngine->GetClient()->AcceptCall(state.peer_id);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeRejectCall(JNIEnv*, jobject) {
  if (g_clientEngine) {
    auto state = g_clientEngine->GetClient()->GetCurrentCallState();
    if (!state.peer_id.IsEmpty()) g_clientEngine->GetClient()->RejectCall(state.peer_id);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeHangupCall(JNIEnv*, jobject) {
  if (g_clientEngine) {
    auto state = g_clientEngine->GetClient()->GetCurrentCallState();
    if (!state.peer_id.IsEmpty()) g_clientEngine->GetClient()->Hangup(state.peer_id, true);
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_MessengerService_nativeMarkAllAsRead(JNIEnv* env, jobject, jstring peer_id) {
  if (g_clientEngine) {
    char const* chars = env->GetStringUTFChars(peer_id, nullptr);
    auto pid = eillance::ContactID(chars);
    env->ReleaseStringUTFChars(peer_id, chars);
    auto unread = g_clientEngine->GetClient()->GetUnreadMessages(pid);
    std::vector<uint64_t> ids;
    ids.reserve(unread.size());
    for (auto const& msg : unread) ids.push_back(msg.id);
    if (!ids.empty()) g_clientEngine->GetClient()->MarkMessagesAsRead(pid, ids);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeTelecomCallEnded(JNIEnv*, jobject) {
  if (g_clientEngine) {
    auto state = g_clientEngine->GetClient()->GetCurrentCallState();
    if (!state.peer_id.IsEmpty()) {
      if (state.is_ringing)
        g_clientEngine->GetClient()->RejectCall(state.peer_id);
      else if (state.is_connecting || state.is_in_call)
        g_clientEngine->GetClient()->Hangup(state.peer_id, true);
    }
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeUpdatePlatformPermissions(
    JNIEnv*,
    jobject,
    jboolean notif,
    jboolean batt,
    jboolean call
) {
  if (g_clientEngine) g_clientEngine->GetClient()->UpdatePlatformPermissions(notif, batt, call);
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeIgnoreBattery(JNIEnv*, jobject) {
  if (g_clientEngine) g_clientEngine->GetClient()->IgnoreBatteryOptimization();
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeIgnoreCalls(JNIEnv*, jobject) {
  if (g_clientEngine) g_clientEngine->GetClient()->IgnoreCallPermissions();
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeResetIgnoredPermissions(JNIEnv*, jobject) {
  if (g_clientEngine) g_clientEngine->GetClient()->ResetIgnoredPermissions();
}

// NOLINTEND(readability-identifier-naming)

jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  g_javaVm = vm;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  Log::InitializeDefault(plog::debug);

  try {
    auto& classref = ClassRefs::Instance();
    classref.RegisterClass(env, "android/content/Context");
    classref.RegisterMethod(env, "android/content/Context", "getFilesDir", "()Ljava/io/File;");
    classref.RegisterClass(env, "java/io/File");
    classref.RegisterMethod(env, "java/io/File", "getPath", "()Ljava/lang/String;");

    // MEDIA TRANSPORT REGISTRATION
    std::string const media_transport_class = "hela/eillance/webrtc/WebRtcMediaTransport";
    classref.RegisterClass(env, media_transport_class);
    classref.RegisterCtor(env, media_transport_class, "(Landroid/content/Context;J)V");
    classref.RegisterMethod(env, media_transport_class, "updateIceServers", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "disconnectPeer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "createOffer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "createAnswer", "(Ljava/lang/String;)V");
    classref
        .RegisterMethod(env, media_transport_class, "getLocalDescription", "(Ljava/lang/String;)Ljava/lang/String;");
    classref.RegisterMethod(env, media_transport_class, "getPeerConnectionState", "(Ljava/lang/String;)I");
    classref.RegisterMethod(
        env,
        media_transport_class,
        "setRemoteDescription",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterMethod(env, media_transport_class, "addVideoTrack", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "addAudioTrack", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "connectPeer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "startLocalMedia", "(Z)V");
    classref.RegisterMethod(env, media_transport_class, "stopLocalMedia", "()V");
    classref.RegisterMethod(env, media_transport_class, "setAudioEnabled", "(Z)V");
    classref.RegisterMethod(env, media_transport_class, "setVideoEnabled", "(Z)V");
    classref.RegisterMethod(env, media_transport_class, "nativeOnSendMediaSignalingMessage", "(JLjava/lang/String;)V");

    classref.RegisterClass(env, "hela/eillance/MessengerService");
    classref.RegisterMethod(
        env,
        "hela/eillance/MessengerService",
        "setWebRtcMediaTransport",
        "(Lhela/eillance/webrtc/WebRtcMediaTransport;)V"
    );
    classref.RegisterMethod(env, "hela/eillance/MessengerService", "onNativeCoreCrash", "()V");

    classref.RegisterClass(env, "hela/eillance/events/NativeEventBus");
    classref
        .RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onContactsUpdated", "(Ljava/util/List;)V");
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onHistoryUpdated",
        "(Ljava/lang/String;Lhela/eillance/ndkio/HistoryPage;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onDraftsUpdated",
        "(Ljava/lang/String;Ljava/util/List;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onTransferProgress",
        "(Ljava/lang/String;Ljava/lang/String;F)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onProfileUpdated",
        "(Lhela/eillance/ndkio/MyProfileInfo;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onCallStateChanged",
        "(Lhela/eillance/ndkio/CallState;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onSettingsUpdated",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ZZZ)V"
    );
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onConnectionStateChanged", "(I)V");
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onPermissionBannerChanged", "(I)V");

    // Explicit Notification Triggers
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onNotifyNewMessage",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onNotifyMissedCall",
        "(Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onNotifyContactRequest",
        "(Ljava/lang/String;Ljava/lang/String;)V"
    );

    // Explicit Hardware / OS Orchestration Commands
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onCmdProcessIncomingTelecom",
        "(Ljava/lang/String;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onCmdProcessOutgoingTelecom",
        "(Ljava/lang/String;Z)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onCmdDisconnectTelecom",
        "(Ljava/lang/String;)V"
    );
    classref.RegisterStaticMethod(
        env,
        "hela/eillance/events/NativeEventBus",
        "onCmdSetTelecomActive",
        "(Ljava/lang/String;)V"
    );
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onCmdSetAudioMode", "(I)V");
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onCmdClearCommunicationDevice", "()V");
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onCmdPlaySound", "(I)V");
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onCmdStopSound", "()V");
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onCmdSetMicrophoneMute", "(Z)V");
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onCmdSetSpeakerphone", "(Z)V");
    classref.RegisterStaticMethod(env, "hela/eillance/events/NativeEventBus", "onPeerVersionMismatch", "(Ljava/lang/String;Z)V");

    classref.RegisterCtor(env, "java/util/ArrayList", "()V");
    classref.RegisterMethod(env, "java/util/ArrayList", "add", "(Ljava/lang/Object;)Z");
    classref.RegisterMethod(env, "java/util/ArrayList", "get", "(I)Ljava/lang/Object;");

    classref.RegisterClass(env, "hela/eillance/ndkio/ReactionEntry");
    classref.RegisterCtor(env, "hela/eillance/ndkio/ReactionEntry", "(Ljava/lang/String;I)V");

    classref.RegisterClass(env, "hela/eillance/ndkio/HistoryEntry");
    classref.RegisterCtor(
        env,
        "hela/eillance/ndkio/HistoryEntry",
        "(JIJLjava/lang/String;IIIIIZZZLjava/util/List;Ljava/util/List;J)V"
    );

    classref.RegisterCtor(
        env,
        "hela/eillance/ndkio/MyProfileInfo",
        "(Ljava/lang/String;ZLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterCtor(
        env,
        "hela/eillance/ndkio/Contact",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIIILjava/lang/String;IIJLjava/lang/"
        "String;II)V"
    );
    classref.RegisterCtor(env, "hela/eillance/ndkio/CallState", "(ZZZILjava/lang/String;IZZI)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/HistoryPage", "(Ljava/util/List;Z)V");

    std::string const draft_class = "hela/eillance/ndkio/DraftAttachment";
    classref.RegisterClass(env, draft_class);
    classref.RegisterCtor(
        env,
        draft_class,
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );

    classref.RegisterCtor(
        env,
        "hela/eillance/ndkio/GlobalSearchResult",
        "(Ljava/lang/String;Lhela/eillance/ndkio/HistoryEntry;)V"
    );

    std::string const video_config_class = "hela/eillance/ndkio/VideoConfig";
    classref.RegisterClass(env, video_config_class);
    classref.RegisterField(env, video_config_class, "cameraId", "I");
    classref.RegisterField(env, video_config_class, "width", "I");
    classref.RegisterField(env, video_config_class, "height", "I");
    classref.RegisterField(env, video_config_class, "frameRate", "I");
    classref.RegisterField(env, video_config_class, "bitrate", "I");

    std::string const attachment_req_class = "hela/eillance/ndkio/AttachmentRequest";
    classref.RegisterClass(env, attachment_req_class);
    classref.RegisterField(env, attachment_req_class, "path", "Ljava/lang/String;");
    classref.RegisterField(env, attachment_req_class, "type", "I");
    classref.RegisterField(env, attachment_req_class, "thumbnail", "Ljava/lang/String;");
    classref.RegisterField(env, attachment_req_class, "mimeType", "Ljava/lang/String;");
    classref.RegisterField(env, attachment_req_class, "note", "Ljava/lang/String;");
    classref.RegisterField(env, attachment_req_class, "fileName", "Ljava/lang/String;");

    std::string const attachment_class = "hela/eillance/ndkio/Attachment";
    classref.RegisterClass(env, attachment_class);
    classref.RegisterCtor(
        env,
        attachment_class,
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;JLjava/lang/String;ZILjava/lang/String;Ljava/lang/"
        "String;)V"
    );

    classref.RegisterClass(env, "hela/eillance/ndkio/AttachmentInfo");
    classref.RegisterCtor(env, "hela/eillance/ndkio/AttachmentInfo", "(Lhela/eillance/ndkio/Attachment;J)V");

    std::string const debug_action_class = "hela/eillance/ndkio/DebugPendingAction";
    classref.RegisterClass(env, debug_action_class);
    classref.RegisterCtor(env, debug_action_class, "(Ljava/lang/String;JLjava/lang/String;)V");

    std::string const debug_conn_class = "hela/eillance/ndkio/DebugConnectionInfo";
    classref.RegisterClass(env, debug_conn_class);
    classref.RegisterCtor(env, debug_conn_class, "(Ljava/lang/String;IJZZ)V");

    std::string const debug_unread_class = "hela/eillance/ndkio/DebugUnreadInfo";
    classref.RegisterClass(env, debug_unread_class);
    classref.RegisterCtor(env, debug_unread_class, "(Ljava/lang/String;IJ)V");

  } catch (std::exception const& e) {
    LGE << "JNI_OnLoad failed: " << e.what();
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
