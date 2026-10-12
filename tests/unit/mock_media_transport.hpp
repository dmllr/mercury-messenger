#pragma once

#include <memory>
#include <string>
#include <vector>

#include "eillance/transport/media_transport.hpp"
#include "mock_call_log.hpp"
#include "rtc/common.hpp"

namespace eillance::transport {

class MockTrack : public ITrack {
public:
  std::vector<std::vector<std::byte>> samples;
  void SendSample(std::vector<std::byte> const& sample, uint64_t /*sample_time*/) override {
    samples.push_back(sample);
  }
};

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

  void StartLocalMedia(bool /*is_video*/) override {
  }
  void StopLocalMedia() override {
  }
  void SetLocalMediaEnabled(bool /*audio*/, bool /*video*/) override {
  }

  auto AddVideo(ContactID const&) -> std::shared_ptr<ITrack> override {
    return std::make_shared<MockTrack>();
  }
  auto AddAudio(ContactID const&) -> std::shared_ptr<ITrack> override {
    return std::make_shared<MockTrack>();
  }
  void AttachToPeer(ContactID const&, std::shared_ptr<Peer> const&) override {
  }
};

}  // namespace eillance::transport
