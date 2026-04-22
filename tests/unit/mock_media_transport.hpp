#pragma once

#include <memory>
#include <string>
#include <vector>

#include "eillance/transport/media_transport.hpp"
#include "mock_call_log.hpp"

namespace eillance::transport {

class MockMediaTransport : public MediaTransport::Interface {
  std::shared_ptr<MockCallLog> log_;

 public:
  explicit MockMediaTransport(std::shared_ptr<MockCallLog> log)
      : log_(std::move(log)) {
  }

  void SetIceServers(std::string const&) override {
  }

  void Connect(ContactID const&) override {
  }
  void Disconnect(ContactID const& peer_id) override {
    log_->LogClose(peer_id, "media");
  }

  void CreateOffer(ContactID const& peer_id) override {
    log_->LogOffer(peer_id, "media");
  }
  void CreateAnswer(ContactID const& peer_id) override {
    log_->LogAnswer(peer_id, "media");
  }
  void SetRemoteDescription(ContactID const&, std::string const&, std::string const&) override {
  }

  auto GetLocalDescription(ContactID const& /*peer_id*/) const -> std::optional<std::string> override {
    return "mock_local_description";
  }

  auto GetConnectionState(ContactID const& /*peer_id*/) const -> PeerConnectionState override {
    return PeerConnectionState::connected;
  }

  void StartLocalMedia(bool /*is_video*/) override {
  }
  void StopLocalMedia() override {
  }
  void SetLocalMediaEnabled(bool /*audio*/, bool /*video*/) override {
  }

  void AddVideo(ContactID const&) override {
  }
  void AddAudio(ContactID const&) override {
  }
};

}  // namespace eillance::transport
