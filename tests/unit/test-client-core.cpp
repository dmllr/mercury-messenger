#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class ClientCoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    mock_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::DataTransport>(std::move(mock_transport_impl_));

    auto media_transport_impl = std::make_unique<transport::MockMediaTransport>(mock_log_);
    auto media_transport = std::make_shared<transport::MediaTransport>(std::move(media_transport_impl));

    client_ = std::make_shared<Client>(storage_, transport_, media_transport);

    (void)client_->GenerateNewIdentity();
    my_id_ = client_->GetId();

    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  void FlushClientQueue() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_transport_impl_;
  transport::MockDataTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;
  std::shared_ptr<Client> client_;
  ContactID my_id_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(ClientCoreTest, ConnectSequence) {
  client_->Connect(peer1);
  FlushClientQueue();

  ASSERT_TRUE(mock_transport_->GetPeer(peer1));

  bool offer_created = false;
  for (auto const& [id, ctx] : mock_transport_->CallLog()->offers) {
    if (id == peer1 && ctx == "data") offer_created = true;
  }
  EXPECT_TRUE(offer_created);
}

TEST_F(ClientCoreTest, IncomingKeyExchangeTriggersReply) {
  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  peer->data_channel_is_open = true;

  crypto::SessionCipher const peer_cipher;
  auto peer_pub_key = peer_cipher.GetPublicKey();

  nlohmann::json const key_msg = {
      {"type", "key_exchange"},
      {"key", std::vector<std::byte>(peer_pub_key.begin(), peer_pub_key.end())},
      {"is_reply", false}
  };

  std::string msg_str = key_msg.dump();
  std::vector<std::byte> msg_bytes(msg_str.size());
  std::transform(msg_str.begin(), msg_str.end(), msg_bytes.begin(), [](char c) { return static_cast<std::byte>(c); });

  eillance::EventQueue::Instance().Push(eillance::EventDataChannelMessage{peer1, msg_bytes});

  FlushClientQueue();

  EXPECT_TRUE(peer->session_cipher->IsReady());

  bool reply_sent = false;
  for (auto const& [id, msg] : mock_transport_->CallLog()->sent_peer_messages) {
    if (id == peer1 && msg["type"] == "key_exchange" && msg["is_reply"] == true) {
      reply_sent = true;
    }
  }
  EXPECT_TRUE(reply_sent);
}

TEST_F(ClientCoreTest, SignalingMessageProcessing_Offer) {
  nlohmann::json const offer_msg =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "data"}, {"sdp", "dummy_sdp_offer"}};

  mock_transport_->SimulatePeerMessage(peer1, offer_msg);
  FlushClientQueue();

  EXPECT_TRUE(mock_transport_->GetPeer(peer1));

  bool answer_created = false;
  for (auto const& [id, ctx] : mock_transport_->CallLog()->answers) {
    if (id == peer1 && ctx == "data") answer_created = true;
  }
  EXPECT_TRUE(answer_created);
}

TEST_F(ClientCoreTest, SignalingMessageProcessing_MediaOffer) {
  nlohmann::json const offer_msg =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "media"}, {"sdp", "dummy_sdp_media_offer_m=audio"}};

  mock_transport_->SimulatePeerMessage(peer1, offer_msg);
  FlushClientQueue();

  EXPECT_TRUE(mock_transport_->GetPeer(peer1));
}

TEST_F(ClientCoreTest, ContactRequestProcessing) {
  nlohmann::json const req_msg = {
      {"from", peer1.ToString()},
      {"type", "contact_add_request"},
      {"profile", {{"display_name", "Peer One"}}}
  };

  mock_transport_->SimulatePeerMessage(peer1, req_msg);
  FlushClientQueue();

  auto contacts = client_->GetContacts();
  ASSERT_EQ(contacts.size(), 1);
  EXPECT_EQ(contacts[0].peer_id, peer1);
  EXPECT_EQ(contacts[0].display_name, "Peer One");
  EXPECT_EQ(contacts[0].status, identity::ContactStatus::awaitingApproval);
}
