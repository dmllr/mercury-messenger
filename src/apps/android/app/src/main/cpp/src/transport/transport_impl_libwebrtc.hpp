#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <jni.h>
#include <nlohmann/json.hpp>
#include <span>

#include "../class_refs.hpp"
#include "../utils/jni_utils.hpp"
#include "eillance/crypto/crypto.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::transport {

class AndroidRtcTrack final : public ITrack {
 public:
  void SendSample(std::vector<std::byte> const& /*sample*/, uint64_t /*sample_time*/) override {
    // No-op on Android, media is handled by the platform\'s WebRTC stack
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
  explicit AndroidRtcTransport(JavaVM* jvm, jobject context)
      : jvm_(jvm) {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (env == nullptr) {
      return;
    }

    auto& transport_class_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    jobject const transport_local_ref = transport_class_holder.NewObject(env, context, reinterpret_cast<jlong>(this));
    transportJavaRef_ = env->NewGlobalRef(transport_local_ref);
    env->DeleteLocalRef(transport_local_ref);

    // Pass the created WebRtcTransport object back to the CallService
    auto& call_service_holder = ClassRefs::Instance().Class("hela/eillance/CallService");
    jmethodID const set_transport_method = call_service_holder.GetMethod("setWebRtcTransport");
    env->CallVoidMethod(context, set_transport_method, transportJavaRef_);
  }

  ~AndroidRtcTransport() override {
    auto const env_scope = GetEnv();
    if (auto* env = env_scope.Get(); (env != nullptr) && transportJavaRef_ != nullptr) {
      env->DeleteGlobalRef(transportJavaRef_);
    }
  }

  void SetDelegate(TransportDelegate* delegate) override { delegate_ = delegate; }
  auto Delegate() -> TransportDelegate* { return delegate_; }

  void SetStatusCallback(Transport::StatusCallbackFunction const& callback) override {
    statusCallback_ = std::make_shared<Transport::StatusCallbackFunction>(callback);
  }
  void ReportStateChange(ContactID const& peer_id, EventSource source, int state) {
    INVARIANT(statusCallback_);
    (*statusCallback_)(peer_id, source, state);
  }
  void SetName(std::string const& /*device_name*/) override {}
  void SetId(ContactID const& id) override { id_ = id; }
  auto GetId() const -> ContactID const& { return id_; }
  void SetSignalingUrl(std::string const& url) override { signalingUrl_ = url; }

  void Start() override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    jmethodID const start_method = transport_holder.GetMethod("connect");
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
    jmethodID const terminate_method = transport_holder.GetMethod("terminate");
    env->CallVoidMethod(transportJavaRef_, terminate_method);
  }

  void Reconnect() override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    jmethodID const reconnect_method = transport_holder.GetMethod("reconnect");
    auto j_url = env->NewStringUTF(signalingUrl_.c_str());
    env->CallVoidMethod(transportJavaRef_, reconnect_method, j_url);
    env->DeleteLocalRef(j_url);
  }

  void AddPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_.emplace(peer_id, peer);
  }
  auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    auto const it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }
  auto GetPeers() const -> std::unordered_map<ContactID, std::shared_ptr<Peer>> override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    return peers_;
  }
  void RemovePeer(ContactID const& peer_id) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_.erase(peer_id);
  }

  auto GetPeerConnectionState(ContactID const& peer_id) const -> eillance::transport::PeerConnectionState override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return eillance::transport::PeerConnectionState::closed;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto const method = transport_holder.GetMethod("getPeerConnectionState");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto const state_val = env->CallIntMethod(transportJavaRef_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return static_cast<eillance::transport::PeerConnectionState>(state_val);
  }

  void SendSignalingMessage(Transport::Json const& message) const override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto const method = transport_holder.GetMethod("sendSignalingMessage");
    auto const msg_str = message.dump();
    auto j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_msg);
    env->DeleteLocalRef(j_msg);
  }

  void SendPeerDataChannelMessage(ContactID const& peer_id, Transport::Json const& message) const override {
    auto const msg_str = message.dump();
    auto bytes = std::vector<std::byte>(msg_str.size());
    std::memcpy(bytes.data(), msg_str.data(), msg_str.size());
    SendPeerDataChannelBinaryMessage(peer_id, bytes);
  }

  auto SendPeerDataChannelBinaryMessage(ContactID const& peer_id, std::span<std::byte const> message) const
      -> bool override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return false;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto const method = transport_holder.GetMethod("sendDataChannelMessageBinary");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());

    jbyteArray j_bytes = env->NewByteArray(message.size());
    env->SetByteArrayRegion(j_bytes, 0, message.size(), reinterpret_cast<jbyte const*>(message.data()));

    jboolean result = env->CallBooleanMethod(transportJavaRef_, method, j_peer_id, j_bytes);

    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_bytes);

    return result == JNI_TRUE;
  }

  void AddToStream(std::shared_ptr<Peer> const& /*peer*/, bool /*is_adding_video*/) const override {}
  void CreateDataPeerConnection(ContactID const& peer_id) override { CreatePeerConnection(peer_id, "data"); }
  void CreateMediaPeerConnection(ContactID const& peer_id) override { CreatePeerConnection(peer_id, "media"); }

  void CreateDataChannel(ContactID const& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    auto const create_dc_method = transport_holder.GetMethod("createDataChannel");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, create_dc_method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void ClosePeerConnection(ContactID const& peer_id, std::string const& context) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) {
      return;
    }
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcTransport");
    jmethodID const close_pc_method = transport_holder.GetMethod("closePeerConnection");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto j_context = env->NewStringUTF(context.c_str());
    env->CallVoidMethod(transportJavaRef_, close_pc_method, j_peer_id, j_context);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_context);
  }

  void CreateOffer(ContactID const& peer_id, std::string const& context) override {
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

  void CreateAnswer(ContactID const& peer_id, std::string const& context) override {
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

  void SetRemoteDescription(
      ContactID const& peer_id,
      std::string const& context,
      std::string const& sdp,
      std::string const& type
  ) override {
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

  void AddRemoteCandidate(
      ContactID const& peer_id,
      std::string const& context,
      std::string const& candidate,
      std::string const& mid
  ) override {
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

  auto AddVideo(ContactID const& peer_id, std::function<void(void)> const& /*on_open*/)
      -> std::shared_ptr<ITrack> override {
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

  auto AddAudio(ContactID const& peer_id, std::function<void(void)> const& /*on_open*/)
      -> std::shared_ptr<ITrack> override {
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
  void CreatePeerConnection(ContactID const& peer_id, std::string const& context) {
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

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerConnectionStateChange(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jint state
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);
    transport->ReportStateChange(peer_id, eillance::EventSource::peerConnectionState, state);
    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingConnectionStateChange(
    JNIEnv* /*env*/,
    jobject,
    jlong native_ptr,
    jint state
) {
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
    if (service_state != eillance::ServiceConnectionState::unknown) {
      transport
          ->ReportStateChange(eillance::ContactID(), eillance::EventSource::system, static_cast<int>(service_state));
    }
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_webrtc_WebRtcTransport_nativeOnPeerMessage(JNIEnv* env, jobject, jlong native_ptr, jstring message) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    char const* msg_chars = env->GetStringUTFChars(message, nullptr);
    try {
      auto const json_msg = nlohmann::json::parse(msg_chars);
      transport->Delegate()->ProcessPeerMessage(json_msg);
    } catch (std::exception const& e) {
      LGE << "Failed to parse peer message: " << e.what();
    }
    env->ReleaseStringUTFChars(message, msg_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSystemMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring type,
    jstring message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    // Convert jstring to std::string for type
    char const* type_chars = env->GetStringUTFChars(type, nullptr);
    std::string const type_str(type_chars);
    env->ReleaseStringUTFChars(type, type_chars);

    // Convert jstring to std::string for message
    char const* msg_chars = env->GetStringUTFChars(message, nullptr);
    std::string const msg_str(msg_chars);
    env->ReleaseStringUTFChars(message, msg_chars);

    try {
      auto const json_msg = nlohmann::json::parse(msg_str);
      transport->Delegate()->OnSystemMessage(type_str, json_msg);
    } catch (std::exception const& e) {
      LGE << "Failed to parse system message: " << e.what();
    }
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnSignalingError(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring peer_id,
    jstring error
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if ((transport != nullptr) && (transport->Delegate() != nullptr)) {
    char const* peer_id_chars = env->GetStringUTFChars(peer_id, nullptr);
    char const* error_chars = env->GetStringUTFChars(error, nullptr);
    transport->Delegate()->OnSignalingErrorForPeer(eillance::ContactID(peer_id_chars), error_chars);
    env->ReleaseStringUTFChars(peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(error, error_chars);
  }
}

// Deprecated in favor of binary handler, but kept for compatibility with legacy messages (if any)
extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelMessage(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jstring j_message
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    char const* message_chars = env->GetStringUTFChars(j_message, nullptr);

    eillance::ContactID const peer_id(peer_id_chars);
    auto const* message_str(message_chars);

    // Wrap string as bytes for uniform handling in core
    auto len = std::strlen(message_str);
    std::vector<std::byte> msg_bytes(len);
    std::memcpy(msg_bytes.data(), message_str, len);

    auto peer = transport->GetPeer(peer_id);
    if (peer && peer->on_data_channel_message) {
      peer->on_data_channel_message(msg_bytes);
    } else {
      LGW << "Received DC message for peer " << peer_id_chars << " but no handler or peer found.";
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
    env->ReleaseStringUTFChars(j_message, message_chars);
  }
}

// New Binary Handler
extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelMessageBinary(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id,
    jbyteArray j_data
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);
    eillance::ContactID const peer_id(peer_id_chars);

    jsize len = env->GetArrayLength(j_data);
    std::vector<std::byte> buffer(len);
    env->GetByteArrayRegion(j_data, 0, len, reinterpret_cast<jbyte*>(buffer.data()));

    auto peer = transport->GetPeer(peer_id);
    if (peer && peer->on_data_channel_message) {
      peer->on_data_channel_message(buffer);
    } else {
      LGW << "Received DC binary message for peer " << peer_id_chars << " but no handler or peer found.";
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}

extern "C" JNIEXPORT void JNICALL Java_hela_eillance_webrtc_WebRtcTransport_nativeOnDataChannelOpen(
    JNIEnv* env,
    jobject,
    jlong native_ptr,
    jstring j_peer_id
) {
  auto* transport = reinterpret_cast<eillance::transport::AndroidRtcTransport*>(native_ptr);
  if (transport != nullptr) {
    char const* peer_id_chars = env->GetStringUTFChars(j_peer_id, nullptr);

    eillance::ContactID const peer_id(peer_id_chars);
    auto peer = transport->GetPeer(peer_id);

    if (peer) {
      LGI << "Data channel with " << peer_id_chars << " opened.";
      peer->data_channel_is_open = true;
      auto my_pub_key = peer->session_cipher->GetPublicKey();
      nlohmann::json const key_exchange_message = {
          {"type", "key_exchange"},
          {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
          {"is_reply", false}
      };
      transport->SendPeerDataChannelMessage(peer_id, key_exchange_message);
    }

    env->ReleaseStringUTFChars(j_peer_id, peer_id_chars);
  }
}
