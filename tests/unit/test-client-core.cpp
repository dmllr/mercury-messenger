#include <chrono>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "eillance/view_model/client_event_router.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class ClientCoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup storage
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    // Setup transport
    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    transport_ = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    client_ = std::make_shared<Client>(storage_, transport_);

    // Initialize Event Router to attach callbacks and satisfy invariants
    router_ = std::make_shared<view_model::ClientEventRouter>(client_);

    // Setup Identity
    (void)client_->GetAccountService()->GenerateNewIdentity();
    my_id_ = client_->GetId();

    // Start Client
    client_->Start();
  }

  void TearDown() override {
    if (router_) router_->DetachFromClient();
    client_->Terminate();
    client_.reset();
    router_.reset();
  }

  // Helper to process pending tasks on the client's internal queue
  void FlushClientQueue() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<Client> client_;
  std::shared_ptr<view_model::ClientEventRouter> router_;
  ContactID my_id_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(ClientCoreTest, ConnectSequence) {
  // 1. Initiate connection
  client_->Connect(peer1);
  FlushClientQueue();

  // Verify peer was added
  ASSERT_TRUE(mock_transport_->GetPeer(peer1));

  // Verify CreateOffer called
  bool offer_created = false;
  for (auto const& [id, ctx] : mock_transport_->log.offers) {
    if (id == peer1 && ctx == "data") offer_created = true;
  }
  EXPECT_TRUE(offer_created);
}

TEST_F(ClientCoreTest, IncomingKeyExchangeTriggersReply) {
  // 1. Connect to setup the peer internally in Client
  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // 2. Simulate Data Channel Opening manually
  // This triggers Client::OnDataChannelOpen which resets the cipher and sends our key.
  // We want to test the RECEPTION of a peer key.
  peer->data_channel_is_open = true;

  // Create a valid peer public key
  crypto::SessionCipher peer_cipher;
  auto peer_pub_key = peer_cipher.GetPublicKey();

  nlohmann::json const key_msg = {
      {"type", "key_exchange"},
      {"key", std::vector<std::byte>(peer_pub_key.begin(), peer_pub_key.end())},
      {"is_reply", false}
  };

  // 3. Simulate receiving this message on data channel
  std::string msg_str = key_msg.dump();
  ASSERT_TRUE(peer->on_data_channel_message) << "Data channel handler not set!";
  peer->on_data_channel_message(msg_str);

  FlushClientQueue();

  // Verify shared secret computed
  EXPECT_TRUE(peer->session_cipher->IsReady());

  // Verify reply sent
  // Note: Client::Connect might have already sent an initial key exchange.
  // We look for one with "is_reply": true.
  bool reply_sent = false;
  for (auto const& [id, msg] : mock_transport_->log.sent_peer_messages) {
    if (id == peer1 && msg["type"] == "key_exchange" && msg["is_reply"] == true) {
      reply_sent = true;
    }
  }
  EXPECT_TRUE(reply_sent);
}

TEST_F(ClientCoreTest, SignalingMessageProcessing_Offer) {
  // Simulate receiving an OFFER via signaling
  nlohmann::json offer_msg =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "data"}, {"sdp", "dummy_sdp_offer"}};

  mock_transport_->SimulatePeerMessage(peer1, offer_msg);
  FlushClientQueue();

  // Should create peer, set remote desc, create answer
  EXPECT_TRUE(mock_transport_->GetPeer(peer1));

  bool answer_created = false;
  for (auto const& [id, ctx] : mock_transport_->log.answers) {
    if (id == peer1 && ctx == "data") answer_created = true;
  }
  EXPECT_TRUE(answer_created);
}

TEST_F(ClientCoreTest, SignalingMessageProcessing_MediaOffer) {
  // Simulate receiving a MEDIA OFFER
  nlohmann::json offer_msg = {
      {"from", peer1.ToString()},
      {"type", "offer"},
      {"context", "media"},
      {"sdp", "dummy_sdp_media_offer_m=audio"}  // implies voice call
  };

  mock_transport_->SimulatePeerMessage(peer1, offer_msg);
  FlushClientQueue();

  // Media offer handling should create the peer if missing and set remote description
  EXPECT_TRUE(mock_transport_->GetPeer(peer1));

  // It also triggers OnCallRequest, which sends a "call_ringing" system message.
  // We mainly check no crash here (invariant violation fixed).
}

TEST_F(ClientCoreTest, ContactRequestProcessing) {
  nlohmann::json req_msg = {
      {"from", peer1.ToString()},
      {"type", "contact_add_request"},
      {"profile", {{"display_name", "Peer One"}}}
  };

  mock_transport_->SimulatePeerMessage(peer1, req_msg);
  FlushClientQueue();

  // Verify contact created
  auto contacts = client_->GetContactService()->GetContacts();
  ASSERT_EQ(contacts.size(), 1);
  EXPECT_EQ(contacts[0].peer_id, peer1);
  EXPECT_EQ(contacts[0].display_name, "Peer One");
  EXPECT_EQ(contacts[0].status, identity::ContactStatus::awaitingApproval);
}
