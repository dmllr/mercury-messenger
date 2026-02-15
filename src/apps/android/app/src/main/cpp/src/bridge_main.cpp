#include <string>

#include <jni.h>

#include "bridge_context.hpp"
#include "eillance/logging/logging.hpp"
#include "utils/class_refs.hpp"

// NOLINTBEGIN(readability-identifier-naming)

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_initEngine(JNIEnv* env, jobject instance) {
  if (g_clientEngine == nullptr) {
    g_clientEngine = std::make_unique<eillance::ClientEngine>(g_javaVm, env, instance);
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

extern "C" JNIEXPORT jboolean JNICALL
Java_hela_eillance_MessengerService_nativePing(JNIEnv* /*env*/, jobject /*instance*/) {
  return g_clientEngine != nullptr;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_MessengerService_nativeRestart(JNIEnv* env, jobject instance) {
  if (g_clientEngine) {
    g_clientEngine->Terminate();
    g_clientEngine.reset();
  }
  g_clientEngine = std::make_unique<eillance::ClientEngine>(g_javaVm, env, instance);
  g_clientEngine->Start();
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

    // DATA TRANSPORT REGISTRATION
    std::string const data_transport_class = "hela/eillance/webrtc/WebRtcDataTransport";
    classref.RegisterClass(env, data_transport_class);
    classref.RegisterCtor(env, data_transport_class, "(Landroid/content/Context;J)V");
    classref.RegisterMethod(env, data_transport_class, "connect", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, data_transport_class, "terminate", "()V");
    classref.RegisterMethod(env, data_transport_class, "reconnect", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, data_transport_class, "updateIceServers", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, data_transport_class, "getPeerConnectionState", "(Ljava/lang/String;)I");
    classref.RegisterMethod(env, data_transport_class, "sendSignalingMessage", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, data_transport_class, "sendDataChannelMessageBinary", "(Ljava/lang/String;[B)Z");
    classref.RegisterMethod(env, data_transport_class, "disconnectPeer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, data_transport_class, "createOffer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, data_transport_class, "createAnswer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(
        env,
        data_transport_class,
        "setRemoteDescription",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterMethod(env, data_transport_class, "connectPeer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, data_transport_class, "nativeOnSignalingConnectionStateChange", "(JI)V");

    // MEDIA TRANSPORT REGISTRATION
    std::string const media_transport_class = "hela/eillance/webrtc/WebRtcMediaTransport";
    classref.RegisterClass(env, media_transport_class);
    classref.RegisterCtor(env, media_transport_class, "(Landroid/content/Context;J)V");
    classref.RegisterMethod(env, media_transport_class, "updateIceServers", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "disconnectPeer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "createOffer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "createAnswer", "(Ljava/lang/String;)V");
    classref.RegisterMethod(
        env,
        media_transport_class,
        "setRemoteDescription",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterMethod(env, media_transport_class, "addVideoTrack", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "addAudioTrack", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, media_transport_class, "connectPeer", "(Ljava/lang/String;)V");

    classref.RegisterClass(env, "hela/eillance/MessengerService");
    classref.RegisterMethod(
        env,
        "hela/eillance/MessengerService",
        "setWebRtcDataTransport",
        "(Lhela/eillance/webrtc/WebRtcDataTransport;)V"
    );
    classref.RegisterMethod(
        env,
        "hela/eillance/MessengerService",
        "setWebRtcMediaTransport",
        "(Lhela/eillance/webrtc/WebRtcMediaTransport;)V"
    );

    // ... (Existing Observers and Data Classes - No Changes needed there) ...
    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeContactsObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeContactsObserver",
        "onContactsUpdated",
        "(Ljava/util/List;)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeConversationObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeConversationObserver",
        "onHistoryUpdated",
        "(Lhela/eillance/ndkio/HistoryPage;)V"
    );
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeConversationObserver",
        "onTransferProgress",
        "(Ljava/lang/String;F)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeProfileObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeProfileObserver",
        "onProfileUpdated",
        "(Lhela/eillance/ndkio/MyProfileInfo;)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeCallObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeCallObserver",
        "onCallStateChanged",
        "(Lhela/eillance/ndkio/CallState;)V"
    );

    classref.RegisterClass(env, "hela/eillance/viewmodels/NativeSettingsObserver");
    classref.RegisterMethod(
        env,
        "hela/eillance/viewmodels/NativeSettingsObserver",
        "onSettingsUpdated",
        "(Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"
    );
    classref.RegisterMethod(env, "hela/eillance/viewmodels/NativeSettingsObserver", "onConnectionStateChanged", "(I)V");

    classref.RegisterCtor(env, "java/util/ArrayList", "()V");
    classref.RegisterMethod(env, "java/util/ArrayList", "add", "(Ljava/lang/Object;)Z");
    classref.RegisterMethod(env, "java/util/ArrayList", "get", "(I)Ljava/lang/Object;");

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
    classref.RegisterCtor(env, "hela/eillance/ndkio/CallState", "(ZZZILjava/lang/String;I)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/HistoryEntry", "(JIJLjava/lang/String;IIIIIZLjava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/HistoryPage", "(Ljava/util/List;Z)V");
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
