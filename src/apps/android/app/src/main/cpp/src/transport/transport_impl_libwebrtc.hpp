#pragma once

#include <jni.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "../utils/jni_utils.hpp"
#include "eillance/crypto/crypto.hpp"
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
  std::string signaling_url_;

  std::unordered_map<std::string, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peers_mutex_;

  auto Attach() const -> JNIEnv* { return eillance::jni::AttachCurrentThreadIfNeeded(jvm_); }

public:
  explicit AndroidRtcTransport(JavaVM* jvm, jobject context) : jvm_(jvm) {
    JNIEnv* env = Attach();
    if (env == nullptr) {
      return;
    }

    jclass transport_class = env->FindClass("hela/eillance/webrtc/WebRtcTransport");
    if (transport_class == nullptr) {
      return;
    }

    jmethodID constructor = env->GetMethodID(transport_class, "<init>", "(Landroid/content/Context;J)V");
    if (constructor == nullptr) {
      return;
    }

    jobject transport_local_ref = env->NewObject(transport_class, constructor, context, reinterpret_cast<jlong>(this));
    transport_java_ref_ = env->NewGlobalRef(transport_local_ref);

    // Pass the created WebRtcTransport object back to the CallService
    jclass call_service_class = env->GetObjectClass(context);
    jmethodID set_transport_method = env->GetMethodID(call_service_class, "setWebRtcTransport", "(Lhela/eillance/webrtc/WebRtcTransport;)V");
    if (set_transport_method != nullptr) {
      env->CallVoidMethod(context, set_transport_method, transport_java_ref_);
    }
  }

  ~AndroidRtcTransport() override {
    JNIEnv* env = Attach();
    if ((env != nullptr) && transport_java_ref_ != nullptr) {
      env->DeleteGlobalRef(transport_java_ref_);
    }
  }

  void SetDelegate(TransportDelegate* delegate) override { delegate_ = delegate; }
  auto Delegate() -> TransportDelegate* { return delegate_; }

  void SetStatusCallback(const Transport::StatusCallbackFunction& callback) override { status_callback_ = std::make_shared<Transport::StatusCallbackFunction>(callback); }
  void ReportStateChange(const std::string& peer_id, EventSource source, int state) {
    if (status_callback_) { (*status_callback_)(peer_id, source, state); }
  }
  void SetName(const std::string& /*device_name*/) override {}
  void SetId(const std::string& id) override { id_ = id; }
  auto GetId() const -> const std::string& { return id_; }
  void SetSignalingUrl(const std::string& url) override { signaling_url_ = url; }

  void Start() override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID start_method = env->GetMethodID(transport_class, "connect", "(Ljava/lang/String;Ljava/lang/String;)V");
    auto j_id = env->NewStringUTF(id_.c_str());
    auto j_url = env->NewStringUTF(signaling_url_.c_str());
    env->CallVoidMethod(transport_java_ref_, start_method, j_id, j_url);
    env->DeleteLocalRef(j_id);
    env->DeleteLocalRef(j_url);
  }

  void Terminate() override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID terminate_method = env->GetMethodID(transport_class, "terminate", "()V");
    env->CallVoidMethod(transport_java_ref_, terminate_method);
  }

  void Reconnect() override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID reconnect_method = env->GetMethodID(transport_class, "reconnect", "(Ljava/lang/String;)V");
    auto j_url = env->NewStringUTF(signaling_url_.c_str());
    env->CallVoidMethod(transport_java_ref_, reconnect_method, j_url);
    env->DeleteLocalRef(j_url);
  }

  void AddPeer(const std::string& peer_id, const std::shared_ptr<Peer>& peer) override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.emplace(peer_id, peer);
  }
  auto GetPeer(const std::string& peer_id) const -> std::shared_ptr<Peer> override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    const auto it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }
  auto GetPeers() const -> std::unordered_map<std::string, std::shared_ptr<Peer>> override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    return peers_;
  }
  void RemovePeer(const std::string& peer_id) override {
    const std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.erase(peer_id);
  }

  auto GetPeerConnectionState(const std::string& peer_id) const -> eillance::transport::PeerConnectionState override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return eillance::transport::PeerConnectionState::Closed;
    }
    auto transport_class = env->GetObjectClass(transport_java_ref_);
    auto method = env->GetMethodID(transport_class, "getPeerConnectionState", "(Ljava/lang/String;)I");
    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto state_val = env->CallIntMethod(transport_java_ref_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return static_cast<eillance::transport::PeerConnectionState>(state_val);
  }

  void SendSignalingMessage(const Transport::Json& message) const override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    auto transport_class = env->GetObjectClass(transport_java_ref_);
    auto method = env->GetMethodID(transport_class, "sendSignalingMessage", "(Ljava/lang/String;)V");
    const auto msg_str = message.dump();
    auto j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transport_java_ref_, method, j_msg);
    env->DeleteLocalRef(j_msg);
  }

  void SendPeerDataChannelMessage(const std::string& peer_id, const Transport::Json& message) const override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    auto transport_class = env->GetObjectClass(transport_java_ref_);
    auto method = env->GetMethodID(transport_class, "sendDataChannelMessage", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (method == nullptr) {
      LOGE << "Failed to find method sendDataChannelMessage";
      return;
    }
    const auto msg_str = message.dump();
    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transport_java_ref_, method, j_peer_id, j_msg);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_msg);
  }

  void AddToStream(const std::shared_ptr<Peer>& /*peer*/, bool /*is_adding_video*/) const override {}
  void CreateDataPeerConnection(const std::string& peer_id) override { CreatePeerConnection(peer_id, "data"); }
  void CreateMediaPeerConnection(const std::string& peer_id) override { CreatePeerConnection(peer_id, "media"); }
  void CreateDataChannel(const std::string& peer_id) override;
  void ClosePeerConnection(const std::string& peer_id, const std::string& context) override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID close_pc_method = env->GetMethodID(transport_class, "closePeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (!close_pc_method) {
      LOGE << "Failed to find method closePeerConnection";
      return;
    }
    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, close_pc_method, j_peer_id, j_context);
  }

  void CreateOffer(const std::string& peer_id, const std::string& context) override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    auto* cls = env->GetObjectClass(transport_java_ref_);
    auto* mid = env->GetMethodID(cls, "createOffer", "(Ljava/lang/String;Ljava/lang/String;)V");
    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void CreateAnswer(const std::string& peer_id, const std::string& context) override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    auto* cls = env->GetObjectClass(transport_java_ref_);
    auto* mid = env->GetMethodID(cls, "createAnswer", "(Ljava/lang/String;Ljava/lang/String;)V");
    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void SetRemoteDescription(const std::string& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    auto* cls = env->GetObjectClass(transport_java_ref_);
    auto* mid = env->GetMethodID(cls, "setRemoteDescription", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto* j_context = env->NewStringUTF(context.c_str());
    auto* j_sdp = env->NewStringUTF(sdp.c_str());
    auto* j_type = env->NewStringUTF(type.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id, j_context, j_sdp, j_type);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
    env->DeleteLocalRef(j_sdp);
    env->DeleteLocalRef(j_type);
  }

  void AddRemoteCandidate(const std::string& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    auto* cls = env->GetObjectClass(transport_java_ref_);
    auto* method_id = env->GetMethodID(cls, "addRemoteCandidate", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto* j_context = env->NewStringUTF(context.c_str());
    auto* j_candidate = env->NewStringUTF(candidate.c_str());
    auto* j_mid = env->NewStringUTF(mid.c_str());
    env->CallVoidMethod(transport_java_ref_, method_id, j_peer_id, j_context, j_candidate, j_mid);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
    env->DeleteLocalRef(j_candidate);
    env->DeleteLocalRef(j_mid);
  }

  auto AddVideo(const std::string& peer_id, const std::function<void(void)>& /*on_open*/) -> std::shared_ptr<ITrack> override {
    JNIEnv* env = Attach();
    if (!env || (transport_java_ref_ == nullptr)) {
      return nullptr;
    }
    auto* cls = env->GetObjectClass(transport_java_ref_);
    auto* mid = env->GetMethodID(cls, "addVideoTrack", "(Ljava/lang/String;)V");
    if (!mid) {
      return nullptr;
    }

    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return std::make_shared<AndroidRtcTrack>();
  }

  auto AddAudio(const std::string& peer_id, const std::function<void(void)>& /*on_open*/) -> std::shared_ptr<ITrack> override {
    JNIEnv* env = Attach();
    if (!env || (transport_java_ref_ == nullptr)) {
      return nullptr;
    }
    auto* cls = env->GetObjectClass(transport_java_ref_);
    auto* mid = env->GetMethodID(cls, "addAudioTrack", "(Ljava/lang/String;)V");
    if (!mid) { return nullptr; }

    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    env->CallVoidMethod(transport_java_ref_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);

    return std::make_shared<AndroidRtcTrack>();
  }

private:

  void CreatePeerConnection(const std::string& peer_id, const std::string& context) {
    JNIEnv* env = Attach();
    if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
      return;
    }
    jclass transport_class = env->GetObjectClass(transport_java_ref_);
    jmethodID create_pc_method = env->GetMethodID(transport_class, "createPeerConnection", "(Ljava/lang/String;Ljava/lang/String;)V");

    auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
    auto* j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transport_java_ref_, create_pc_method, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }
};

inline void AndroidRtcTransport::CreateDataChannel(const std::string& peer_id) {
  JNIEnv* env = Attach();
  if ((env == nullptr) || (transport_java_ref_ == nullptr)) {
    return;
  }
  auto transport_class = env->GetObjectClass(transport_java_ref_);
  auto create_dc_method = env->GetMethodID(transport_class, "createDataChannel", "(Ljava/lang/String;)V");
  if (create_dc_method == nullptr) {
    LOGE << "Failed to find method createDataChannel";
    return;
  }
  auto* j_peer_id = env->NewStringUTF(peer_id.c_str());
  env->CallVoidMethod(transport_java_ref_, create_dc_method, j_peer_id);
  env->DeleteLocalRef(j_peer_id);
}


}  // namespace eillance::transport

// --- JNI Callbacks ---

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerConnectionStateChange(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id, jint state) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    const std::string peer_id_str(peer_id_chars);
    transport->ReportStateChange(peer_id_str, eillance::EventSource::peerConnectionState, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerMessage(JNIEnv* env, jobject, jlong native_ptr, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      const auto json_msg = nlohmann::json::parse(msg_chars);
      transport->Delegate()->processPeerMessage(json_msg);
    } catch (const std::exception& e) {
      LOGE << "Failed to parse peer message: " << e.what();
    }
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSystemMessage(JNIEnv* env, jobject, jlong native_ptr, jstring type, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    // Convert jstring to std::string for type
    const char* type_chars = env->GetStringUTFChars(type, nullptr);
    const std::string type_str(type_chars);
    env->ReleaseStringUTFChars(type, type_chars);

    // Convert jstring to std::string for message
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    const std::string msg_str(msg_chars);
    env->ReleaseStringUTFChars(message, msg_chars);

    try {
      const auto json_msg = nlohmann::json::parse(msg_str);

      if (type_str == "auth_challenge") {
        const auto challenge = json_msg.value("challenge", "");
        if (!challenge.empty()) {
          const auto signature_bytes = transport->Delegate()->Sign(challenge);
          const auto signature_hex = eillance::crypto::ToHex(signature_bytes);
          const nlohmann::json response = {{"type", "auth_response"}, {"id", transport->GetId()}, {"signature", signature_hex}};
          transport->SendSignalingMessage(response);
        }
      } else if (type_str == "login_success") {
        transport->Delegate()->onLoginSuccess();
        transport->ReportStateChange("", eillance::EventSource::system, static_cast<int>(eillance::ServiceConnectionState::online));
      } else {
        transport->Delegate()->processSystemMessage(type_str, json_msg);
      }
    } catch (const std::exception& e) {
      LOGE << "Failed to parse system message: " << e.what();
    }
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingError(JNIEnv* env, jobject, jlong native_ptr, jstring peer_id, jstring error) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    const char* error_chars = env->GetStringUTFChars(error, nullptr);
    transport->Delegate()->onSignalingErrorForPeer(peer_id_chars, error_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(error, error_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelMessage(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id, jstring j_message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    const char* message_chars = env->GetStringUTFChars(j_message, nullptr);
    const auto* peer_id_str(peer_id_chars);
    const auto* message_str(message_chars);

    auto peer = transport->GetPeer(peer_id_str);
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
  if (transport != nullptr) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    const auto* peer_id_str(peer_id_chars);
    auto peer = transport->GetPeer(peer_id_str);

    if (peer) {
      LOGI << "Data channel with " << peer_id_str << " opened.";
      peer->data_channel_is_open_ = true;
      auto my_pub_key = peer->session_cipher_->GetPublicKey();
      const nlohmann::json key_exchange_message = {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())}, {"is_reply", false}};
      transport->SendPeerDataChannelMessage(peer_id_str, key_exchange_message);
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}
