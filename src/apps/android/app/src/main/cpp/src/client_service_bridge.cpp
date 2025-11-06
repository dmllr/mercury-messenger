#include <cassert>
#include <jni.h>
#include <memory>
#include <string>

#include "class_refs.hpp"
#include "client_engine.hpp"

namespace {
JavaVM* java_vm = nullptr;
std::unique_ptr<eillance::ClientEngine> client_engine{};
}  // namespace

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_initEngine(JNIEnv* env, jobject instance, jobject native_bridge) {
  // The 'instance' here is the CallService, which is a Context.
  if (client_engine == nullptr) {
    client_engine = std::make_unique<eillance::ClientEngine>(java_vm, env, instance, native_bridge);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_start(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine) { client_engine->Start(); }
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_CallService_identityExists(JNIEnv* /*env*/, jobject /*instance*/) {
    return client_engine && client_engine->ViewModel() && client_engine->ViewModel()->GetState().identity_exists;
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_terminate(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine) {
    client_engine->Terminate();
    client_engine.reset();
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_refreshState(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->RefreshState(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_call(JNIEnv* env, jobject /*instance*/, jstring peer_id) {
  const char* chars = env->GetStringUTFChars(peer_id, nullptr);
  const std::string peer_id_str(chars);
  env->ReleaseStringUTFChars(peer_id, chars);
  if (client_engine) { client_engine->Call(peer_id_str); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_addContact(JNIEnv* env, jobject /*instance*/, jstring peer_id) {
  const char* chars = env->GetStringUTFChars(peer_id, nullptr);
  const std::string peer_id_str(chars);
  env->ReleaseStringUTFChars(peer_id, chars);
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->AddContact(peer_id_str); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_acceptContact(JNIEnv* env, jobject /*instance*/, jstring peer_id) {
  const char* chars = env->GetStringUTFChars(peer_id, nullptr);
  const std::string peer_id_str(chars);
  env->ReleaseStringUTFChars(peer_id, chars);
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->AcceptContact(peer_id_str); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_removeContact(JNIEnv* env, jobject /*instance*/, jstring peer_id) {
  const char* chars = env->GetStringUTFChars(peer_id, nullptr);
  const std::string peer_id_str(chars);
  env->ReleaseStringUTFChars(peer_id, chars);
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->RemoveContact(peer_id_str); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_selectContact(JNIEnv* env, jobject /*instance*/, jstring peer_id) {
  const char* chars = env->GetStringUTFChars(peer_id, nullptr);
  const std::string peer_id_str(chars);
  env->ReleaseStringUTFChars(peer_id, chars);
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->SelectContact(peer_id_str); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_sendMessage(JNIEnv* env, jobject /*instance*/, jstring message) {
  const char* chars = env->GetStringUTFChars(message, nullptr);
  const std::string message_str(chars);
  env->ReleaseStringUTFChars(message, chars);
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->SendMessage(message_str); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_startVoiceCall(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->StartVoiceCall(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_startVideoCall(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->StartVideoCall(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_hangup(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->Hangup(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeAcceptCall(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->AcceptCall(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeRejectCall(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->RejectCall(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeUpdateSignalingUrl(JNIEnv* env, jobject /*instance*/, jstring url) {
  if (client_engine && client_engine->ViewModel()) {
    const char* chars = env->GetStringUTFChars(url, nullptr);
    const std::string url_str(chars);
    env->ReleaseStringUTFChars(url, chars);
    client_engine->ViewModel()->UpdateSignalingUrl(url_str);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeUpdateMyProfile(JNIEnv* env, jobject /*instance*/, jstring name, jstring bio, jstring photo_base64) {
  if (client_engine && client_engine->ViewModel()) {
    const char* c_name_chars = (name != nullptr) ? env->GetStringUTFChars(name, nullptr) : nullptr;
    const char* c_bio_chars = (bio != nullptr) ? env->GetStringUTFChars(bio, nullptr) : nullptr;
    const char* c_photo_chars = (photo_base64 != nullptr) ? env->GetStringUTFChars(photo_base64, nullptr) : nullptr;

    const auto c_name = (c_name_chars) ? std::optional<std::string>(c_name_chars) : std::nullopt;
    const auto c_bio = (c_bio_chars) ? std::optional<std::string>(c_bio_chars) : std::nullopt;
    const auto c_photo = (c_photo_chars) ? std::optional<std::string>(c_photo_chars) : std::nullopt;

    client_engine->ViewModel()->UpdateMyProfile(c_name, c_bio, c_photo);

    if (c_name_chars) { env->ReleaseStringUTFChars(name, c_name_chars); }
    if (c_bio_chars) { env->ReleaseStringUTFChars(bio, c_bio_chars); }
    if (c_photo_chars) { env->ReleaseStringUTFChars(photo_base64, c_photo_chars); }
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeSetContactDisplayNameOverride(JNIEnv* env, jobject /*instance*/, jstring peer_id, jstring name) {
  if (client_engine && client_engine->ViewModel()) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    const char* name_chars = env->GetStringUTFChars(name, nullptr);
    client_engine->ViewModel()->SetContactDisplayNameOverride(peer_id_chars, name_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(name, name_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeResetContactDisplayNameOverride(JNIEnv* env, jobject /*instance*/, jstring peer_id) {
  if (client_engine && client_engine->ViewModel()) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    client_engine->ViewModel()->ResetContactDisplayNameOverride(peer_id_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
  }
}


extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_loadOlderMessages(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->LoadOlderMessages(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_loadFullMessage(JNIEnv* /*env*/, jobject /*instance*/, jlong message_id) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->LoadFullMessage(static_cast<std::uint64_t>(message_id)); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_markMessagesAsRead(JNIEnv* env, jobject /*instance*/, jlongArray message_ids) {
  if (client_engine && client_engine->ViewModel()) {
    if (message_ids == nullptr) {
      LOGW << "markMessagesAsRead called with a null message_ids array.";
      return;
    }

    if (auto state = client_engine->ViewModel()->GetState(); !state.selected_contact_id) { return; }

    std::vector<std::uint64_t> ids;
    jsize count = env->GetArrayLength(message_ids);
    if (count == 0) { return; }
    ids.reserve(count);

    jlong* elements = env->GetLongArrayElements(message_ids, nullptr);
    if (elements != nullptr) {
      for (jsize i = 0; i < count; i++) {
        ids.push_back(static_cast<std::uint64_t>(elements[i]));
      }
      env->ReleaseLongArrayElements(message_ids, elements, JNI_ABORT);
    } else {
      // Error handling for GetLongArrayElements failure
      if (env->ExceptionCheck()) {
        LOGE << "Failed to get long array elements. JNI exception occurred.";
        env->ExceptionDescribe();
        env->ExceptionClear();
      } else {
        LOGE << "GetLongArrayElements returned null without a pending exception.";
      }
      return;
    }
    client_engine->ViewModel()->MarkMessagesAsRead(ids);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_configureVideoCapture(JNIEnv* env, jobject /*instance*/, jobject video_config) {
  if (client_engine) {
    auto& video_config_holder = ClassRefs::Instance().Class("hela/eillance/ndkio/VideoConfig");
    auto conf = eillance::VideoConfig{};
    conf.camera_id = env->GetIntField(video_config, video_config_holder.GetField("cameraId"));
    conf.width = env->GetIntField(video_config, video_config_holder.GetField("width"));
    conf.height = env->GetIntField(video_config, video_config_holder.GetField("height"));
    conf.frame_rate = env->GetIntField(video_config, video_config_holder.GetField("frameRate"));
    conf.bitrate = env->GetIntField(video_config, video_config_holder.GetField("bitrate"));
    client_engine->ConfigureVideoCapture(conf);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeDebugEraseAllHistory(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->DebugEraseAllHistory(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeDebugEraseAllContacts(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->DebugEraseAllContacts(); }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_CallService_nativeDebugRevokeAllApprovals(JNIEnv* /*env*/, jobject /*instance*/) {
  if (client_engine && client_engine->ViewModel()) { client_engine->ViewModel()->DebugRevokeAllApprovals(); }
}

extern "C" JNIEXPORT jstring JNICALL Java_hela_eillance_CallService_nativeGetMnemonic(JNIEnv* env, jobject /*instance*/) {
    if (client_engine && client_engine->ViewModel()) {
        if (auto mnemonic = client_engine->ViewModel()->GetMnemonic()) {
            return env->NewStringUTF(mnemonic->c_str());
        }
    }
    return nullptr;
}

extern "C" JNIEXPORT jstring JNICALL Java_hela_eillance_CallService_nativeGenerateNewIdentity(JNIEnv* env, jobject /*instance*/) {
    if (client_engine && client_engine->ViewModel()) {
        std::string mnemonic = client_engine->ViewModel()->GenerateNewIdentity();
        return env->NewStringUTF(mnemonic.c_str());
    }
    return env->NewStringUTF("");
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_CallService_nativeRestoreIdentity(JNIEnv* env, jobject /*instance*/, jstring mnemonic) {
    if (client_engine && client_engine->ViewModel()) {
        const char* chars = env->GetStringUTFChars(mnemonic, nullptr);
        const std::string mnemonic_str(chars);
        env->ReleaseStringUTFChars(mnemonic, chars);
        return client_engine->ViewModel()->RestoreIdentity(mnemonic_str);
    }
    return false;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_CallService_nativeCreateBackup(JNIEnv* env, jobject /*instance*/, jstring path, jstring password) {
    if (client_engine && client_engine->ViewModel()) {
        const char* path_chars = env->GetStringUTFChars(path, nullptr);
        const char* pw_chars = env->GetStringUTFChars(password, nullptr);
        bool result = client_engine->ViewModel()->CreateBackup(path_chars, pw_chars);
        env->ReleaseStringUTFChars(path, path_chars);
        env->ReleaseStringUTFChars(password, pw_chars);
        return result;
    }
    return false;
}

extern "C" JNIEXPORT jboolean JNICALL Java_hela_eillance_CallService_nativeRestoreBackup(JNIEnv* env, jobject /*instance*/, jstring path, jstring password) {
    // The implementation is identical to createBackup, just calling a different ViewModel method.
    // This can be refactored into a helper if more such functions are added.
    if (client_engine && client_engine->ViewModel()) {
        const char* path_chars = env->GetStringUTFChars(path, nullptr);
        const char* pw_chars = env->GetStringUTFChars(password, nullptr);
        bool result = client_engine->ViewModel()->RestoreBackup(path_chars, pw_chars);
        env->ReleaseStringUTFChars(path, path_chars);
        env->ReleaseStringUTFChars(password, pw_chars);
        return result;
    }
    return false;
}

jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  java_vm = vm;
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) { return JNI_ERR; }

  Log::InitializeDefault(plog::debug);

  try {
    auto& classref = ClassRefs::Instance();
    classref.RegisterClass(env, "android/content/Context");
    classref.RegisterMethod(env, "android/content/Context", "getFilesDir", "()Ljava/io/File;");
    classref.RegisterClass(env, "java/io/File");
    classref.RegisterMethod(env, "java/io/File", "getPath", "()Ljava/lang/String;");

    // Register CallService and its methods
    const std::string call_service_class = "hela/eillance/CallService";
    classref.RegisterClass(env, call_service_class);
    classref.RegisterMethod(env, call_service_class, "setWebRtcTransport", "(Lhela/eillance/webrtc/WebRtcTransport;)V");
    classref.RegisterMethod(env, call_service_class, "onStateUpdate", "(Ljava/lang/String;)V");

    // Register WebRtcTransport and its methods
    const std::string webrtc_transport_class = "hela/eillance/webrtc/WebRtcTransport";
    classref.RegisterClass(env, webrtc_transport_class);
    classref.RegisterCtor(env, webrtc_transport_class, "(Landroid/content/Context;J)V");
    classref.RegisterMethod(env, webrtc_transport_class, "connect", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "terminate", "()V");
    classref.RegisterMethod(env, webrtc_transport_class, "reconnect", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "getPeerConnectionState", "(Ljava/lang/String;)I");
    classref.RegisterMethod(env, webrtc_transport_class, "sendSignalingMessage", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "sendDataChannelMessage", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "closePeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "createOffer", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "createAnswer", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "setRemoteDescription", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "addRemoteCandidate", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "addVideoTrack", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "addAudioTrack", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "createPeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "createDataChannel", "(Ljava/lang/String;)V");
    classref.RegisterMethod(env, webrtc_transport_class, "nativeOnSignalingConnectionStateChange", "(JI)V");

    // Register other ctors
    classref.RegisterCtor(env, "java/util/ArrayList", "()V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Peer", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Camera", "(Ljava/lang/String;Ljava/lang/String;Ljava/util/List;)V");
    classref.RegisterCtor(env, "hela/eillance/ndkio/Resolution", "(II)V");

    // Register VideoConfig and its fields
    const std::string video_config_class = "hela/eillance/ndkio/VideoConfig";
    classref.RegisterClass(env, video_config_class);
    classref.RegisterField(env, video_config_class, "cameraId", "I");
    classref.RegisterField(env, video_config_class, "width", "I");
    classref.RegisterField(env, video_config_class, "height", "I");
    classref.RegisterField(env, video_config_class, "frameRate", "I");
    classref.RegisterField(env, video_config_class, "bitrate", "I");
  } catch (const std::exception& e) {
    LOGE << "JNI_OnLoad failed: " << e.what();
    return JNI_ERR;
  }

  return JNI_VERSION_1_6;
}
