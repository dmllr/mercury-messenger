#pragma once

#include <jni.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "../utils/jni_utils.hpp"
#include "eillance/logging/logging.h"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/transport.hpp"

namespace eillance::transport {

class AndroidRtcTrack final : public ITrack {
public:
  void SendSample(const std::vector<std::byte>& /*sample*/, uint64_t /*sample_time*/) override {
    // No-op on Android, media is handled by the platform's WebRTC stack
  }
};

class AndroidRtcTransport final : public Transport::TransportInterface {
  JavaVM* jvm_;
  jobject transport_java_ref_{nullptr};
  TransportDelegate* delegate_{nullptr};
  std::shared_ptr<Transport::StatusCallbackFunction> status_callback_{};
  std::string id_;

  std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peers_mutex_;

  auto Attach() const -> JNIEnv* { return eillance::jni::AttachCurrentThreadIfNeeded(jvm_); }

public:
  explicit AndroidRtcTransport(JavaVM* jvm, jobject context) : jvm_(jvm) {
    JNIEnv* env = Attach();
    if (!env) { return; }

    jclass transport_class = env->FindClass("hela/eillance/webrtc/WebRtcTransport");
    if (!transport_class) { return; }

    jmethodID constructor = env->GetMethodID(transport_class, "<init>", "(Landroid/content/Context;J)V");
    if (!constructor) { return; }

    jobject transport_local_ref = env->NewObject(transport_class, constructor, context, reinterpret_cast<jlong>(this));
    transport_java_ref_ = env->NewGlobalRef(transport_local_ref);

    // Pass the created WebRtcTransport object back to the CallService
    jclass call_service_class = env->GetObjectClass(context);
    jmethodID set_transport_method = env->GetMethodID(call_service_class, "setWebRtcTransport", "(Lhela/eillance/webrtc/WebRtcTransport;)V");
    if (set_transport_method) { env->CallVoidMethod(context, set_transport_method, transport_java_ref_); }
  }

  ~AndroidRtcTransport() override {
    JNIEnv* env = Attach();
    if (env && transport_java_ref_) { env->DeleteGlobalRef(transport_java_ref_); }
  }

  void setDelegate(TransportDelegate* delegate) override { delegate_ = delegate; }
  auto delegate() -> TransportDelegate* { return delegate_; }

  void setStatusCallback(const Transport::StatusCallbackFunction& callback) override { status_callback_ = std::make_shared<Transport::StatusCallbackFunction>(callback); }
  void reportStateChange(const std::string& peer_id, EventSource source, int state) {
    if (status_callback_) { (*status_callback_)(peer_id, source, state); }
  }
  void setName(const std::string& /*device_name*/) override {}
  void setId(const std::string& id) override { id_ = id; }

  void start() override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID start_method = env->GetMethodID(transport_class, "connect", "(Ljava/lang/String;)V");
    jstring j_id = env->NewStringUTF(id_.c_str());
    env->CallVoidMethod(transport_java_ref_, start_method, j_id);
    env->DeleteLocalRef(j_id);
  }

  void terminate() override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID terminate_method = env->GetMethodID(transport_class, "terminate", "()V");
    env->CallVoidMethod(transport_java_ref_, terminate_method);
  }

  void addPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.emplace(peer_id, peer);
  }
  auto getPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    const auto it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }
  auto getPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    return peers_;
  }
  void removePeer(const std::string& peer_id) override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.erase(peer_id);
  }


  auto getPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return eillance::transport::PeerConnectionState::Closed; }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID method = env->GetMethodID(transport_class, "getPeerConnectionState", "(Ljava/lang/String;)I");
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jint state_val = env->CallIntMethod(transport_java_ref_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return static_cast<eillance::transport::PeerConnectionState>(state_val);
  }

  void sendSignalingMessage(const Transport::Json& message) const override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID method = env->GetMethodID(transport_class, "sendSignalingMessage", "(Ljava/lang/String;)V");
    std::string msg_str = message.dump();
    jstring j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transport_java_ref_, method, j_msg);
    env->DeleteLocalRef(j_msg);
  }

  void sendPeerDataChannelMessage(const std::string& peer_id, const Transport::Json& message) const override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID method = env->GetMethodID(transport_class, "sendDataChannelMessage", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (!method) {
      LOGE << "Failed to find method sendDataChannelMessage";
      return;
    }
    std::string msg_str = message.dump();
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transport_java_ref_, method, j_peer_id, j_msg);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_msg);
  }

  void addToStream(const std::shared_ptr<Peer>& /*peer*/, bool /*is_adding_video*/) const override {}
  void createDataPeerConnection(const std::string& peer_id) override { createPeerConnection(peer_id, "data"); }
  void createMediaPeerConnection(const std::string& peer_id) override { createPeerConnection(peer_id, "media"); }
  void createDataChannel(const std::string& peer_id) override;
  void closePeerConnection(const std::string& peer_id, const std::string& context) override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID close_pc_method = env->GetMethodID(transport_class, "closePeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (!close_pc_method) {
      LOGE << "Failed to find method closePeerConnection";
      return;
    }
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, close_pc_method, j_peer_id, j_context);
  }

  void createOffer(const std::string& peer_id, const std::string& context) override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass cls = env->GetObjectClass(transport_java_ref_);
    jmethodID mid = env->GetMethodID(cls, "createOffer", "(Ljava/lang/String;Ljava/lang/String;)V");
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void createAnswer(const std::string& peer_id, const std::string& context) override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass cls = env->GetObjectClass(transport_java_ref_);
    jmethodID mid = env->GetMethodID(cls, "createAnswer", "(Ljava/lang/String;Ljava/lang/String;)V");
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void setRemoteDescription(const std::string& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass cls = env->GetObjectClass(transport_java_ref_);
    jmethodID mid = env->GetMethodID(cls, "setRemoteDescription", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_context = env->NewStringUTF(context.c_str());
    jstring j_sdp = env->NewStringUTF(sdp.c_str());
    jstring j_type = env->NewStringUTF(type.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id, j_context, j_sdp, j_type);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
    env->DeleteLocalRef(j_sdp);
    env->DeleteLocalRef(j_type);
  }

  void addRemoteCandidate(const std::string& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass cls = env->GetObjectClass(transport_java_ref_);
    jmethodID method_id = env->GetMethodID(cls, "addRemoteCandidate", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_context = env->NewStringUTF(context.c_str());
    jstring j_candidate = env->NewStringUTF(candidate.c_str());
    jstring j_mid = env->NewStringUTF(mid.c_str());
    env->CallVoidMethod(transport_java_ref_, method_id, j_peer_id, j_context, j_candidate, j_mid);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
    env->DeleteLocalRef(j_candidate);
    env->DeleteLocalRef(j_mid);
  }

  auto addVideo(const std::string& peer_id, const std::function<void(void)>& /*on_open*/) -> std::shared_ptr<ITrack> override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return nullptr; }
    jclass cls = env->GetObjectClass(transport_java_ref_);
    jmethodID mid = env->GetMethodID(cls, "addVideoTrack", "(Ljava/lang/String;)V");
    if (!mid) { return nullptr; }

    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return std::make_shared<AndroidRtcTrack>();
  }
  auto addAudio(const std::string& peer_id, const std::function<void(void)>& /*on_open*/) -> std::shared_ptr<ITrack> override {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return nullptr; }
    jclass cls = env->GetObjectClass(transport_java_ref_);
    jmethodID mid = env->GetMethodID(cls, "addAudioTrack", "(Ljava/lang/String;)V");
    if (!mid) { return nullptr; }

    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);

    return std::make_shared<AndroidRtcTrack>();
  }

private:
  void createPeerConnection(const std::string& peer_id, const std::string& context) {
    JNIEnv* env = Attach();
    if (!env || !transport_java_ref_) { return; }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID create_pc_method = env->GetMethodID(transport_class, "createPeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");

    jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
    jstring j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, create_pc_method, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }
};

inline void AndroidRtcTransport::createDataChannel(const std::string& peer_id) {
  JNIEnv* env = Attach();
  if (!env || !transport_java_ref_) { return; }
  jclass transport_class = env->GetObjectClass(transport_java_ref_);
  jmethodID create_dc_method = env->GetMethodID(transport_class, "createDataChannel", "(Ljava/lang/String;)V");
  if (!create_dc_method) {
    LOGE << "Failed to find method createDataChannel";
    return;
  }
  jstring j_peer_id = env->NewStringUTF(peer_id.c_str());
  env->CallVoidMethod(transport_java_ref_, create_dc_method, j_peer_id);
  env->DeleteLocalRef(j_peer_id);
}


}  // namespace eillance::transport

// --- JNI Callbacks ---

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerConnectionStateChange(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id, jint state) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    std::string peer_id_str(peer_id_chars);
    transport->reportStateChange(peer_id_str, eillance::EventSource::peerConnectionState, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerMessage(JNIEnv* env, jobject, jlong native_ptr, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      nlohmann::json json_msg = nlohmann::json::parse(msg_chars);
      transport->delegate()->processPeerMessage(json_msg);
    } catch (const std::exception& e) {
      LOGE << "Failed to parse peer message: " << e.what();
    }
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSystemMessage(JNIEnv* env, jobject, jlong native_ptr, jstring type, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* type_chars = env->GetStringUTFChars(type, nullptr);
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      nlohmann::json json_msg = nlohmann::json::parse(msg_chars);
      transport->delegate()->processSystemMessage(type_chars, json_msg);
    } catch (const std::exception& e) {
      LOGE << "Failed to parse system message: " << e.what();
    }
    env->ReleaseStringUTFChars(type, type_chars);
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingError(JNIEnv* env, jobject, jlong native_ptr, jstring peer_id, jstring error) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport && transport->delegate()) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    const char* error_chars = env->GetStringUTFChars(error, nullptr);
    transport->delegate()->onSignalingErrorForPeer(peer_id_chars, error_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(error, error_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelMessage(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id, jstring j_message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    const char* message_chars = env->GetStringUTFChars(j_message, nullptr);
    std::string peer_id_str(peer_id_chars);
    std::string message_str(message_chars);

    auto peer = transport->getPeer(peer_id_str);
    if (peer && peer->on_data_channel_message_) {
      peer->on_data_channel_message_(message_str);
    } else {
      LOGW << "Received DC message for peer " << peer_id_str << " but no handler or peer found.";
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(j_message, message_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelOpen(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    std::string peer_id_str(peer_id_chars);
    auto peer = transport->getPeer(peer_id_str);

    if (peer) {
      LOGI << "Data channel with " << peer_id_str << " opened.";
      peer->data_channel_is_open_ = true;
      auto my_pub_key = peer->session_cipher_->GetPublicKey();
      const nlohmann::json key_exchange_message = {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())}, {"is_reply", false}};
      transport->sendPeerDataChannelMessage(peer_id_str, key_exchange_message);
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}
