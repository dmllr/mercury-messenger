#include <algorithm>
#include <memory>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class MockCapture {
 public:
  void Configure(Config) {
  }
  void Engage() {
  }
  void Revoke() {
  }
  auto FetchBuffers() -> Packet {
    return Packet();
  }
  auto GetSampleDuration() -> Time::Unit {
    return 10000;
  }
};

class InternalMessagingServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    client_ = std::make_shared<Client>(storage_, transport_, media_transport);
    client_->SetVideoCapture(std::make_unique<MockCapture>());
    client_->SetAudioCapture(std::make_unique<MockCapture>());
    std::ignore = client_->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<Client> client_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(InternalMessagingServiceTest, ProcessSystemMessage_AuthChallenge) {
  std::string challenge = "random_challenge_string";
  nlohmann::json const msg = {{"challenge", challenge}};

  EventQueue::Instance().Push(EventSystemMessage{"auth_challenge", msg});
  EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        return mock_log_->sent_signaling_messages.size() > 0;
      },
      std::chrono::milliseconds(2000)
  );

  ASSERT_EQ(mock_transport_->CallLog()->sent_signaling_messages.size(), 1);
  auto sent = mock_transport_->CallLog()->sent_signaling_messages[0];
  EXPECT_EQ(sent["type"], "auth_response");
  EXPECT_FALSE(sent["signature"].get<std::string>().empty());
}

TEST_F(InternalMessagingServiceTest, ProcessSignalingMessage_DataOffer) {
  nlohmann::json const offer =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "data"}, {"sdp", "fake_sdp"}};
  EventQueue::Instance().Push(EventPeerMessage{offer});
  EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, ctx] : mock_log_->answers) {
          if (pid == peer1 && ctx == "data") return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool answer_created = false;
  for (auto const& [pid, ctx] : mock_transport_->CallLog()->answers) {
    if (pid == peer1 && ctx == "data") {
      answer_created = true;
    }
  }
  EXPECT_TRUE(answer_created);
}

TEST_F(InternalMessagingServiceTest, ProcessSignalingMessage_SyncPropose) {
  auto const peer = std::make_shared<transport::Peer>(peer1);
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  transport_->AddPeer(peer1, peer);

  nlohmann::json const propose =
      {{"from", peer1.ToString()}, {"type", "sync_propose"}, {"topic", peer1.ToString()}, {"head", "000"}};
  EventQueue::Instance().Push(EventPeerMessage{propose});
  // Wait for sync_provide or confirm
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& msg : mock_log_->sent_peer_messages | std::views::values) {
          if (msg.value("type", "") == "sync_provide" || msg.value("type", "") == "sync_confirm") {
            return true;
          }
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );
  // Assert something was sent
  EXPECT_GT(mock_transport_->CallLog()->sent_peer_messages.size(), 0);
}
