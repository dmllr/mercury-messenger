#pragma once

#include <memory>
#include <string>

#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/track.hpp"

namespace eillance::transport {

class MediaTransport {
 public:
  struct Interface {
    virtual ~Interface() = default;
    virtual void SetIceServers(std::string const& json_config) = 0;

    // Actions
    virtual void Connect(ContactID const& peer_id) = 0;  // Create Media PeerConnection
    virtual void Disconnect(ContactID const& peer_id) = 0;

    virtual void CreateOffer(ContactID const& peer_id) = 0;
    virtual void CreateAnswer(ContactID const& peer_id) = 0;
    virtual void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) = 0;

    virtual auto GetLocalDescription(ContactID const& peer_id) const -> std::optional<std::string> = 0;
    virtual auto GetConnectionState(ContactID const& peer_id) const -> PeerConnectionState = 0;

    // Media Resource Control
    virtual void StartLocalMedia(bool is_video) = 0;
    virtual void StopLocalMedia() = 0;
    virtual void SetLocalMediaEnabled(bool audio, bool video) = 0;

    virtual auto AddVideo(ContactID const& peer_id) -> std::shared_ptr<ITrack> = 0;
    virtual auto AddAudio(ContactID const& peer_id) -> std::shared_ptr<ITrack> = 0;
    virtual void AttachToPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) = 0;
  };

  template <typename T>
  explicit MediaTransport(std::unique_ptr<T> impl)
      : self_(std::move(impl)) {
  }

  void SetIceServers(std::string const& json_config) const {
    self_->SetIceServers(json_config);
  }

  void Connect(ContactID const& peer_id) const {
    self_->Connect(peer_id);
  }
  void Disconnect(ContactID const& peer_id) const {
    self_->Disconnect(peer_id);
  }

  void CreateOffer(ContactID const& peer_id) const {
    self_->CreateOffer(peer_id);
  }
  void CreateAnswer(ContactID const& peer_id) const {
    self_->CreateAnswer(peer_id);
  }
  void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) const {
    self_->SetRemoteDescription(peer_id, sdp, type);
  }

  [[nodiscard]] auto GetConnectionState(ContactID const& peer_id) const -> PeerConnectionState {
    return self_->GetConnectionState(peer_id);
  }

  [[nodiscard]] auto GetLocalDescription(ContactID const& peer_id) const -> std::optional<std::string> {
    return self_->GetLocalDescription(peer_id);
  }

  void StartLocalMedia(bool is_video) const {
    self_->StartLocalMedia(is_video);
  }
  void StopLocalMedia() const {
    self_->StopLocalMedia();
  }
  void SetLocalMediaEnabled(bool audio, bool video) const {
    self_->SetLocalMediaEnabled(audio, video);
  }

  auto AddVideo(ContactID const& peer_id) const -> std::shared_ptr<ITrack> {
    return self_->AddVideo(peer_id);
  }
  auto AddAudio(ContactID const& peer_id) const -> std::shared_ptr<ITrack> {
    return self_->AddAudio(peer_id);
  }
  void AttachToPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) const {
    self_->AttachToPeer(peer_id, peer);
  }

 private:
  std::unique_ptr<Interface> self_;
};

}  // namespace eillance::transport
