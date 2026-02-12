#pragma once

#include <functional>
#include <memory>
#include <string>

#include "eillance/events/event_source.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/transport/track.hpp"
#include "eillance/transport/transport_delegate.hpp"

namespace eillance::transport {

class MediaTransport {
 public:
  using StatusCallbackFunction = std::function<void(ContactID const& peer_id, EventSource, int)>;

  struct Interface {
    virtual ~Interface() = default;
    virtual void SetStatusCallback(StatusCallbackFunction const& callback) = 0;
    virtual void SetIceServers(std::string const& json_config) = 0;

    // Actions
    virtual void Connect(ContactID const& peer_id) = 0;  // Create Media PeerConnection
    virtual void Disconnect(ContactID const& peer_id) = 0;

    virtual void CreateOffer(ContactID const& peer_id) = 0;
    virtual void CreateAnswer(ContactID const& peer_id) = 0;
    virtual void SetRemoteDescription(ContactID const& peer_id, std::string const& sdp, std::string const& type) = 0;

    // Media
    virtual auto AddVideo(ContactID const& peer_id, std::function<void(void)> const& on_open)
        -> std::shared_ptr<ITrack> = 0;
    virtual auto AddAudio(ContactID const& peer_id, std::function<void(void)> const& on_open)
        -> std::shared_ptr<ITrack> = 0;
    virtual void AttachToPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) = 0;
  };

  template <typename T>
  explicit MediaTransport(std::unique_ptr<T> impl)
      : self_(std::move(impl)) {
  }

  void SetStatusCallback(StatusCallbackFunction const& callback) const {
    self_->SetStatusCallback(callback);
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

  auto AddVideo(ContactID const& peer_id, std::function<void(void)> const& on_open) const -> std::shared_ptr<ITrack> {
    return self_->AddVideo(peer_id, on_open);
  }
  auto AddAudio(ContactID const& peer_id, std::function<void(void)> const& on_open) const -> std::shared_ptr<ITrack> {
    return self_->AddAudio(peer_id, on_open);
  }
  void AttachToPeer(ContactID const& peer_id, std::shared_ptr<Peer> const& peer) const {
    self_->AttachToPeer(peer_id, peer);
  }

 private:
  std::unique_ptr<Interface> self_;
};

}  // namespace eillance::transport
