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
#include "eillance/transport/media_transport.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.hpp"
#include "eillance/utils/invariant.hpp"
#include "utils/class_refs.hpp"
#include "utils/jni_utils.hpp"

namespace eillance::transport {

class AndroidRtcTrack final : public ITrack {
 public:
  void SendSample(std::vector<std::byte> const&, uint64_t) override {
    // No-op on Android, media is handled by the platform's WebRTC stack
  }
};

class AndroidMediaTransport final : public MediaTransport::Interface {
  JavaVM* jvm_;
  jobject transportJavaRef_{nullptr};
  std::shared_ptr<MediaTransport::StatusCallbackFunction> statusCallback_{};

  std::unordered_map<ContactID, std::shared_ptr<Peer>> peers_;
  mutable std::mutex peersMutex_;

  [[nodiscard]] auto GetEnv() const -> eillance::jni::ScopedJniEnv {
    return eillance::jni::ScopedJniEnv(jvm_);
  }

 public:
  explicit AndroidMediaTransport(JavaVM* jvm, jobject context)
      : jvm_(jvm) {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (env == nullptr) return;

    auto& transport_class_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    jobject const transport_local_ref = transport_class_holder.NewObject(env, context, reinterpret_cast<jlong>(this));
    transportJavaRef_ = env->NewGlobalRef(transport_local_ref);
    env->DeleteLocalRef(transport_local_ref);

    auto& call_service_holder = ClassRefs::Instance().Class("hela/eillance/CallService");
    jmethodID const set_transport_method = call_service_holder.GetMethod("setWebRtcMediaTransport");
    env->CallVoidMethod(context, set_transport_method, transportJavaRef_);
  }

  ~AndroidMediaTransport() override {
    auto const env_scope = GetEnv();
    if (auto* env = env_scope.Get(); (env != nullptr) && transportJavaRef_ != nullptr) {
      env->DeleteGlobalRef(transportJavaRef_);
    }
  }

  void SetStatusCallback(MediaTransport::StatusCallbackFunction const& callback) override {
    statusCallback_ = std::make_shared<MediaTransport::StatusCallbackFunction>(callback);
  }
  void ReportStateChange(ContactID const& peer_id, EventSource source, int state) {
    if (statusCallback_) (*statusCallback_)(peer_id, source, state);
  }

  void SetIceServers(std::string const& json_config) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    jmethodID const method = transport_holder.GetMethod("updateIceServers");
    auto* j_json = env->NewStringUTF(json_config.c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_json);
    env->DeleteLocalRef(j_json);
  }

  void AttachToPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) override {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    peers_.emplace(peer_id, peer);
  }

  auto GetPeer(ContactID const& peer_id) const -> std::shared_ptr<Peer> {
    std::lock_guard<std::mutex> const lock(peersMutex_);
    auto const it = peers_.find(peer_id);
    return (it != peers_.end()) ? it->second : nullptr;
  }

  void Connect(ContactID const& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    auto* method = transport_holder.GetMethod("connectPeer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void Disconnect(ContactID const& peer_id) override {
    {
      std::lock_guard<std::mutex> const lock(peersMutex_);
      if (auto peer = peers_.find(peer_id); peer != peers_.end()) {
        peer->second->has_media_peer_connection = false;
        peers_.erase(peer);
      }
    }

    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    jmethodID const method = transport_holder.GetMethod("disconnectPeer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, method, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void CreateOffer(ContactID const& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    auto* mid = transport_holder.GetMethod("createOffer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void CreateAnswer(ContactID const& peer_id) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    auto* mid = transport_holder.GetMethod("createAnswer");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
  }

  void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if ((env == nullptr) || (transportJavaRef_ == nullptr)) return;
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    auto* mid = transport_holder.GetMethod("setRemoteDescription");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    auto* j_sdp = env->NewStringUTF(sdp.c_str());
    auto* j_type = env->NewStringUTF(type.c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id, j_sdp, j_type);
    env->DeleteLocalRef(j_peer_id);
    env->DeleteLocalRef(j_sdp);
    env->DeleteLocalRef(j_type);
  }

  auto AddVideo(ContactID const& peer_id, std::function<void(void)> const&) -> std::shared_ptr<ITrack> override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (!env || (transportJavaRef_ == nullptr)) return nullptr;
    Connect(peer_id);
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    auto* mid = transport_holder.GetMethod("addVideoTrack");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return std::make_shared<AndroidRtcTrack>();
  }

  auto AddAudio(ContactID const& peer_id, std::function<void(void)> const&) -> std::shared_ptr<ITrack> override {
    auto env_scope = GetEnv();
    auto* env = env_scope.Get();
    if (!env || (transportJavaRef_ == nullptr)) return nullptr;
    Connect(peer_id);
    auto& transport_holder = ClassRefs::Instance().Class("hela/eillance/webrtc/WebRtcMediaTransport");
    auto* mid = transport_holder.GetMethod("addAudioTrack");
    auto* j_peer_id = env->NewStringUTF(peer_id.ToString().c_str());
    env->CallVoidMethod(transportJavaRef_, mid, j_peer_id);
    env->DeleteLocalRef(j_peer_id);
    return std::make_shared<AndroidRtcTrack>();
  }
};

}  // namespace eillance::transport
