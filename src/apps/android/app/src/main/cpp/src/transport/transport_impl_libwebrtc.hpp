#pragma once

#include <jni.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "../class_refs.hpp"
#include "../utils/jni_utils.hpp"
#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
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
  jobject transportJavaRef_{nullptr};
  TransportDelegate* delegate_{nullptr};
  std::shared_ptr<Transport::StatusCallbackFunction> statusCallback_{};
  ContactID id_;
  std::string signalingUrl_;

  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peersMutex_;

  [[nodiscard]] auto GetEnv() const -> eillance::jni::ScopedJniEnv { return eillance::jni::ScopedJniEnv(jvm_); }

public:
  explicit AndroidRtcTransport(JavaVM* jvm, jobject context) : jvm_(jvm) {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (env == nullptr) {
      return;
    }

    auto& transport_class_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const jobject transport_local_ref = transport_class_holder.NewObject(env, context, reinterpret_cast<jlong>(this));
    transportJavaRef_ = env->NewGlobalRef(transport_local_ref);
    env->DeleteLocalRef(transport_local_ref);

    // Pass the created WebRtcTransport object back to the CallService
    auto& call_service_holder = ClassRefs::Instance().Class("hela/eillance/CallService");
    const jmethodID set_transport_method = call_service_holder.GetMethod("setWebRtcTransport");
    env->CallVoidMethod(context, set_transport_method, transportJavaRef_);
  }

  ~AndroidRtcTransport() override {
    const auto env_scope = GetEnv();
    if (auto* env = env_scope.Get(); (env != nullptr) && transportJavaRef_ != nullptr) {
      env->DeleteGlobalRef(transportJavaRef_);
    }
  }

  void SetDelegate(TransportDelegate* delegate) override { delegate_ = delegate; }
  auto Delegate() -> TransportDelegate* { return delegate_; }

  void SetStatusCallback(const Transport::StatusCallbackFunction& callback) override { statusCallback_ = std::make_shared<Transport::StatusCallbackFunction>(callback); }
  void ReportStateChange(const ContactID& peer_id, EventSource source, int state) {
    if (statusCallback_) { (*statusCallback_)(peer_id, source, state); }
  }
  void SetName(const std::string& /*device_name*/) override {}
  void SetId(const ContactID& id) override { id_ = id; }
  auto GetId() const -> const ContactID& { return id_; }
  void SetSignalingUrl(const std::string& url) override { signalingUrl_ = url; }

  void Start() override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const jmethodID start_method = transport_holder.GetMethod("connect");
    auto j_id = env->NewStringUTF(id_.ToString().c_str());
    auto j_url = env->NewStringUTF(signalingUrl_.c_str());
    env->CallVoidMethod(transportJavaRef_, start_method, j_id, j_url);
    env->DeleteLocalRef(j_id);
    env->DeleteLocalRef(j_url);
  }

  void Terminate() override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const jmethodID terminate_method = transport_holder.GetMethod("terminate");
    env->CallVoidMethod(transportJavaRef_, terminate_method);
  }

  void Reconnect() override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const jmethodID reconnect_method = transport_holder.GetMethod("reconnect");
    auto j_url = env->NewStringUTF(signalingUrl_.c_str());
    env->CallVoidMethod(transportJavaRef_, reconnect_method, j_url);
    env->DeleteLocalRef(j_url);
  }

  void AddPeer(const ContactID& peer_id, const std::shared_ptr<Peer>& peer) override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    peers_.emplace(peer_id, peer);
  }
  auto GetPeer(const ContactID& peer_id) const -> std::shared_ptr<Peer> override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    const auto it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }
  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    return peers_;
  }
  void RemovePeer(const ContactID& peer_id) override {
    const std::lock_guard<std::mutex> lock(peersMutex_);
    peers_.erase(peer_id);
  }

  auto GetPeerConnectionState(const ContactID& peer_id) const -> eillance::transport::PeerConnectionState override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return eillance::transport::PeerConnectionState::closed;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const auto method = transport_holder.GetMethod("getPeerConnectionState");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    const auto state_val = env->CallIntMethod(transportJavaRef_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return static_cast<eillance::transport::PeerConnectionState>(state_val);
  }

  void SendSignalingMessage(const Transport::Json& message) const override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const auto method = transport_holder.GetMethod("sendSignalingMessage");
    const auto msg_str = message.dump();
    auto j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_msg);
    env->DeleteLocalRef(j_msg);
  }

  void SendPeerDataChannelMessage(const ContactID& peer_id, const Transport::Json& message) const override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const auto method = transport_holder.GetMethod("sendDataChannelMessage");
    const auto msg_str = message.dump();
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_peer_id, j_msg);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_msg);
  }

  void AddToStream(const std::shared_ptr<Peer>& /*peer*/, bool /*is_adding_video*/) const override {}
  void CreateDataPeerConnection(const ContactID& peer_id) override { CreatePeerConnection(peer_id, "data"); }
  void CreateMediaPeerConnection(const ContactID& peer_id) override { CreatePeerConnection(peer_id, "media"); }

  void CreateDataChannel(const ContactID& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const auto create_dc_method = transport_holder.GetMethod("createDataChannel");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, create_dc_method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void ClosePeerConnection(const ContactID& peer_id, const std::string& context) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    const jmethodID close_pc_method = transport_holder.GetMethod("closePeerConnection");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transportJavaRef_, close_pc_method, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void CreateOffer(const ContactID& peer_id, const std::string& context) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto* mid = transport_holder.GetMethod("createOffer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void CreateAnswer(const ContactID& peer_id, const std::string& context) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto* mid = transport_holder.GetMethod("createAnswer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void SetRemoteDescription(const ContactID& peer_id, const std::string& context, const std::string& sdp, const std::string& type) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto* mid = transport_holder.GetMethod("setRemoteDescription");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto* j_context = env->NewStringUTF(context.c_str());
    auto* j_sdp = env->NewStringUTF(sdp.c_str());
    auto* j_type = env->NewStringUTF(type.c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id, j_context, j_sdp, j_type);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
    env->DeleteLocalRef(j_sdp);
    env->DeleteLocalRef(j_type);
  }

  void AddRemoteCandidate(const ContactID& peer_id, const std::string& context, const std::string& candidate, const std::string& mid) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto* method_id = transport_holder.GetMethod("addRemoteCandidate");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto* j_context = env->NewStringUTF(context.c_str());
    auto* j_candidate = env->NewStringUTF(candidate.c_str());
    auto* j_mid = env->NewStringUTF(mid.c_str());
    env->CallVoidMethod(transportJavaRef_, method_id, j_peer_id, j_context, j_candidate, j_mid);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
    env->DeleteLocalRef(j_candidate);
    env->DeleteLocalRef(j_mid);
  }

  auto AddVideo(const ContactID& peer_id, const std::function<void(void)>& /*on_open*/) -> std::shared_ptr<ITrack> override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (!env || (transportJavaRef_ == nullptr)) {
      return nullptr;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto* mid = transport_holder.GetMethod("addVideoTrack");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return std::make_shared<AndroidRtcTrack>();
  }

  auto AddAudio(const ContactID& peer_id, const std::function<void(void)>& /*on_open*/) -> std::shared_ptr<ITrack> override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (!env || (transportJavaRef_ == nullptr)) {
      return nullptr;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto* mid = transport_holder.GetMethod("addAudioTrack");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);

    return std::make_shared<AndroidRtcTrack>();
  }

private:
  void CreatePeerConnection(const ContactID& peer_id, const std::string& context) {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto* create_pc_method = transport_holder.GetMethod("createPeerConnection");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto* j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transportJavaRef_, create_pc_method, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }
};

}  // namespace eillance::transport

// --- JNI Callbacks ---

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerConnectionStateChange(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id, jint state) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    const eillance::ContactID peer_id(peer_id_chars);
    transport->ReportStateChange(peer_id, eillance::EventSource::peerConnectionState, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingConnectionStateChange(JNIEnv* /*env*/, jobject, jlong native_ptr, jint state) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    auto service_state = eillance::ServiceConnectionState::unknown;
    switch (state) {
      case 0:  // CONNECTING
      case 1:  // OPEN
        service_state = eillance::ServiceConnectionState::establishing;
        break;
      case 2:  // CLOSED
        service_state = eillance::ServiceConnectionState::disconnected;
        break;
      case 3:  // FAILED
        service_state = eillance::ServiceConnectionState::failure;
        break;
      default:
        break;
    }
    if (service_state != eillance::ServiceConnectionState::unknown) { transport->ReportStateChange(eillance::ContactID(), eillance::EventSource::system, static_cast<int>(service_state)); }
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerMessage(JNIEnv* env, jobject, jlong native_ptr, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      const auto json_msg = nlohmann::json::parse(msg_chars);
      transport->Delegate()->ProcessPeerMessage(json_msg);
    } catch (const std::exception& e) {
      LGE << "Failed to parse peer message: " << e.what();
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
      transport->Delegate()->OnSystemMessage(type_str, json_msg);
    } catch (const std::exception& e) {
      LGE << "Failed to parse system message: " << e.what();
    }
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingError(JNIEnv* env, jobject, jlong native_ptr, jstring peer_id, jstring error) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    const char* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    const char* error_chars = env->GetStringUTFChars(error, nullptr);
    transport->Delegate()->OnSignalingErrorForPeer(eillance::ContactID(peer_id_chars), error_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(error, error_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelMessage(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id, jstring j_message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    const char* message_chars = env->GetStringUTFChars(j_message, nullptr);

    const eillance::ContactID peer_id(peer_id_chars);
    const auto* message_str(message_chars);

    auto peer = transport->GetPeer(peer_id);
    if (peer && peer->on_data_channel_message) {
      peer->on_data_channel_message(message_str);
    } else {
      LGW << "Received DC message for peer " << peer_id_chars << " but no handler or peer found.";
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(j_message, message_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelOpen(JNIEnv* env, jobject, jlong native_ptr, jstring j_peer_id) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    const char* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);

    const eillance::ContactID peer_id(peer_id_chars);
    auto peer = transport->GetPeer(peer_id);

    if (peer) {
      LGI << "Data channel with " << peer_id_chars << " opened.";
      peer->data_channel_is_open = true;
      auto my_pub_key = peer->session_cipher->GetPublicKey();
      const nlohmann::json key_exchange_message = {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())}, {"is_reply", false}};
      transport->SendPeerDataChannelMessage(peer_id, key_exchange_message);
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}
