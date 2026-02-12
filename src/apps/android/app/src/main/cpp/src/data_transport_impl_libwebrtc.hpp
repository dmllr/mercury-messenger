#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <jni.h>
#include <nlohmann/json.hpp>
#include <span>

#include "eillance/crypto/crypto.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.hpp"
#include "eillance/utils/invariant.hpp"
#include "utils/class_refs.hpp"
#include "utils/jni_utils.hpp"

namespace eillance::transport {

class AndroidDataTransport final : public DataTransport::Interface {
  using Json = nlohmann::json;

  JavaVM* jvm_;
  jobject transportJavaRef_{nullptr};
  TransportDelegate* delegate_{nullptr};
  std::shared_ptr<DataTransport::StatusCallbackFunction> statusCallback_{};
  ContactID id_;
  std::string signalingUrl_;

  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peersMutex_;

  [[nodiscard]] auto GetEnv() const -> eillance::jni::ScopedJniEnv {
    return eillance::jni::ScopedJniEnv(jvm_);
  }

 public:
  explicit AndroidDataTransport(JavaVM* jvm, jobject context)
      : jvm_(jvm) {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (env == nullptr) return;

    auto& transport_class_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    jobject const transport_local_ref = transport_class_holder.NewObject(env, context, reinterpret_cast<jlong>(this));
    transportJavaRef_ = env->NewGlobalRef(transport_local_ref);
    env->DeleteLocalRef(transport_local_ref);

    auto& call_service_holder = ClassRefs::Instance().Class("hela/eillance/CallService");
    jmethodID const set_transport_method = call_service_holder.GetMethod("setWebRtcDataTransport");
    env->CallVoidMethod(context, set_transport_method, transportJavaRef_);
  }

  ~AndroidDataTransport() override {
    auto const env_scope = GetEnv();
    if (auto* env = env_scope.Get(); (env != nullptr) && transportJavaRef_ != nullptr) {
      env->DeleteGlobalRef(transportJavaRef_);
    }
  }

  void SetDelegate(TransportDelegate* delegate) override {
    delegate_ = delegate;
  }
  auto Delegate() -> TransportDelegate* {
    return delegate_;
  }

  void SetStatusCallback(DataTransport::StatusCallbackFunction const& callback) override {
    statusCallback_ = std::make_shared<DataTransport::StatusCallbackFunction>(callback);
  }
  void ReportStateChange(ContactID const& peer_id, EventSource source, int state) {
    INVARIANT(statusCallback_);
    (*statusCallback_)(peer_id, source, state);
  }

  void SetId(ContactID const& id) override {
    id_ = id;
  }
  void SetSignalingUrl(std::string const& url) override {
    signalingUrl_ = url;
  }

  void Start() override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
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
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    jmethodID const terminate_method = transport_holder.GetMethod("terminate");
    env->CallVoidMethod(transportJavaRef_, terminate_method);
  }

  void Reconnect() override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    jmethodID const reconnect_method = transport_holder.GetMethod("reconnect");
    auto j_url = env->NewStringUTF(signalingUrl_.c_str());
    env->CallVoidMethod(transportJavaRef_, reconnect_method, j_url);
    env->DeleteLocalRef(j_url);
  }

  void SetIceServers(std::string const& json_config) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    jmethodID const method = transport_holder.GetMethod("updateIceServers");
    auto* j_json = env->NewStringUTF(json_config.c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_json);
    env->DeleteLocalRef(j_json);
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

  auto GetConnectionState(ContactID const& peer_id) const -> PeerConnectionState override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (auto peer = GetPeer(peer_id); peer && peer->data_channel_is_open) return PeerConnectionState::connected;
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return PeerConnectionState::closed;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    auto const method = transport_holder.GetMethod("getPeerConnectionState");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto const state_val = env->CallIntMethod(transportJavaRef_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return static_cast<PeerConnectionState>(state_val);
  }

  void Connect(ContactID const& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    auto* method = transport_holder.GetMethod("connectPeer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void Disconnect(ContactID const& peer_id) override {
    if (auto peer = GetPeer(peer_id)) peer->data_channel_is_open = false;
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    jmethodID const method = transport_holder.GetMethod("disconnectPeer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void CreateOffer(ContactID const& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    auto* mid = transport_holder.GetMethod("createOffer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void CreateAnswer(ContactID const& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    auto* mid = transport_holder.GetMethod("createAnswer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    auto* mid = transport_holder.GetMethod("setRemoteDescription");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto* j_sdp = env->NewStringUTF(sdp.c_str());
    auto* j_type = env->NewStringUTF(type.c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id, j_sdp, j_type);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_sdp);
    env->DeleteLocalRef(j_type);
  }

  auto SendData(ContactID const& peer_id, std::span<std::byte const> message) const -> bool override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return false;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    auto const method = transport_holder.GetMethod("sendDataChannelMessageBinary");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    jbyteArray j_bytes = env->NewByteArray(message.size());
    env->SetByteArrayRegion(j_bytes, 0, message.size(), reinterpret_cast<jbyte const*>(message.data()));
    jboolean result = env->CallBooleanMethod(transportJavaRef_, method, j_peer_id, j_bytes);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_bytes);
    return result == JNI_TRUE;
  }

  auto SendJson(ContactID const& peer_id, Json const& message) const -> bool override {
    auto const msg_str = message.dump();
    auto bytes = std::vector<std::byte>(msg_str.size());
    std::memcpy(bytes.data(), msg_str.data(), msg_str.size());
    return SendData(peer_id, bytes);
  }

  void SendSignalingMessage(Json const& message) const override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcDataTransport");
    auto const method = transport_holder.GetMethod("sendSignalingMessage");
    auto const msg_str = message.dump();
    auto j_msg = env->NewStringUTF(msg_str.c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_msg);
    env->DeleteLocalRef(j_msg);
  }
};

}  // namespace eillance::transport
