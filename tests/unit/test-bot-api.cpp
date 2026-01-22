#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "eillance/capture/config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/utils/packet.hpp"
#include "mercury/bot/bot.hpp"
#include "mock_persistent_storage.hpp"
#include "mock_transport.hpp"

using namespace eillance;

class MockCapture {
 public:
  void Configure(Config) {}
  void Engage() {}
  void Revoke() {}
  auto FetchBuffers() -> Packet { return Packet(); }
  auto GetSampleDuration() -> Time::Unit { return 10000; }
};

class BotApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup Core Client components
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    auto storage = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_transport_impl_ = std::make_unique<transport::MockTransport>();
    mock_transport_ = mock_transport_impl_.get();
    auto transport = std::make_shared<transport::Transport>(std::move(mock_transport_impl_));

    client_ = std::make_shared<Client>(storage, transport);

    // Fix: Inject Mock Captures to satisfy av_stream_ invariant in MediaCallService
    client_->SetVideoCapture(std::make_unique<MockCapture>());
    client_->SetAudioCapture(std::make_unique<MockCapture>());

    // Fix: Set callbacks to avoid invariant violations
    client_->GetAccountService()->SetOnMyProfileUpdatedCallback([]() {});
    client_->GetContactService()->SetContactRequestCallback([](auto const&) {});
    client_->GetContactService()->SetContactStatusChangedCallback([](auto const&, auto) {});
    client_->GetContactService()->SetContactProfileUpdatedCallback([](auto const&) {});
    client_->GetContactService()->SetContactRemovedCallback([](auto const&) {});
    client_->GetContactService()->SetPeerListUpdateCallback([](auto const&) {});
    client_->GetContactService()->SetProfileReceivedCallback([](auto const&, auto const&) {});
    client_->GetMediaCallService()->SetIncomingCallCallback([](auto const&, auto) {});
    client_->GetMediaCallService()->SetCallRingingCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallAcceptedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetCallTerminatedCallback([](auto const&) {});
    client_->GetMediaCallService()->SetFrameReceivedCallback([](auto const&) {});
    client_->GetMessagingService()->SetChatMessageReceivedCallback([](auto const&, auto const&) {});
    client_->GetMessagingService()->SetMessageStatusChangedCallback([](auto const&, auto, auto) {});
    client_->SetStatusCallback([](auto const&, auto, auto) {});

    // Initialize Identity
    (void)client_->GetAccountService()->GenerateNewIdentity();

    // Start Client logic
    client_->Start();

    // Setup Bot with injected Client
    mercury::bot::Bot::Options options;
    options.identity_path = "dummy_path";  // Not used with injected client
    options.auto_accept_contacts = true;
    options.auto_reject_calls = true;

    bot_ = std::make_unique<mercury::bot::Bot>(client_, options);
  }

  void TearDown() override {
    if (bot_) {
      bot_->Stop();
    }
    if (client_) {
      client_->Terminate();
    }
  }

  void FlushClientQueue() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::unique_ptr<transport::MockTransport> mock_transport_impl_;
  transport::MockTransport* mock_transport_{};
  std::shared_ptr<Client> client_;
  std::unique_ptr<mercury::bot::Bot> bot_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(BotApiTest, AutoAcceptContacts) {
  // Simulate receiving a contact add request
  nlohmann::json req_msg = {
      {"from", peer1.ToString()},
      {"type", "contact_add_request"},
      {"profile", {{"display_name", "Peer One"}}}
  };

  mock_transport_->SimulatePeerMessage(peer1, req_msg);
  FlushClientQueue();

  // Verify contact is confirmed (Auto-accept)
  auto contact = client_->GetContactService()->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);

  // Check that confirmation was sent
  bool confirmed = false;
  for (auto const& msg : mock_transport_->log.sent_signaling_messages) {
    if (msg["type"] == "contact_confirm" && msg["to"] == peer1.ToString()) {
      confirmed = true;
    }
  }
  EXPECT_TRUE(confirmed);
}

TEST_F(BotApiTest, AutoRejectCalls) {
  // Simulate incoming call
  // This usually comes via signaling "offer" with context="media"
  nlohmann::json offer_msg =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "media"}, {"sdp", "dummy_sdp_media"}};

  mock_transport_->SimulatePeerMessage(peer1, offer_msg);
  FlushClientQueue();

  // Bot logic calls client->RejectCall() -> Hangup(notify=true)
  // Verify hangup message sent via data channel (if open) or resources cleaned
  // Since data channel is not open in this mock scenario without extra setup,
  // we primarily check that the transport closed the media connection.

  bool media_closed = false;
  for (auto const& [pid, ctx] : mock_transport_->log.closed_connections) {
    if (pid == peer1 && ctx == "media") media_closed = true;
  }
  EXPECT_TRUE(media_closed);
}

TEST_F(BotApiTest, MessageHandling) {
  bool message_received = false;
  std::string received_text;

  bot_->OnMessage([&](mercury::bot::Message const& msg) {
    message_received = true;
    received_text = msg.text;
  });

  // Use Client::Connect to properly setup peer handlers (including on_data_channel_message)
  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  // Setup data channel and crypto manually on the created peer
  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());

  // Simulate encrypted message arrival
  std::string plain = "Hello Bot";
  auto encrypted = other.Encrypt(plain);
  nlohmann::json const chat_msg = {
      {"type", "chat"},
      {"message_id", 123},
      {"is_system", false},
      {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
  };

  // Inject via the handler that Client::Connect established
  ASSERT_TRUE(peer->on_data_channel_message);
  peer->on_data_channel_message(chat_msg.dump());

  FlushClientQueue();

  EXPECT_TRUE(message_received);
  EXPECT_EQ(received_text, "Hello Bot");
}

TEST_F(BotApiTest, SendMessage) {
  // Ensure peer is a contact so sending is allowed
  client_->AddContact(peer1, std::nullopt);
  // Ensure peer is connected so message sends immediately
  client_->Connect(peer1);
  FlushClientQueue();

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  peer->data_channel_is_open = true;
  peer->session_cipher->ComputeSharedSecret(eillance::crypto::SessionCipher().GetPublicKey());

  bot_->SendMessage(peer1.ToString(), "Reply from Bot");
  FlushClientQueue();

  bool sent = false;
  for (auto const& [pid, msg] : mock_transport_->log.sent_peer_messages) {
    if (pid == peer1 && msg["type"] == "chat") {
      sent = true;
    }
  }
  EXPECT_TRUE(sent);
}
