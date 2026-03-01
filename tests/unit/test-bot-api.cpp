#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/capture/config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/utils/packet.hpp"
#include "mercury/bot/bot.hpp"
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

class BotApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    auto storage = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();

    mock_data_transport_impl_ = std::make_unique<transport::MockDataTransport>(mock_log_);
    mock_data_transport_raw_ = mock_data_transport_impl_.get();
    auto data_transport = std::make_shared<transport::DataTransport>(std::move(mock_data_transport_impl_));

    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    client_ = std::make_shared<Client>(storage, data_transport, media_transport);

    client_->SetVideoCapture(std::make_unique<MockCapture>());
    client_->SetAudioCapture(std::make_unique<MockCapture>());

    (void)client_->GetAccountService()->GenerateNewIdentity();

    client_->Start();

    mercury::bot::Bot::Options options;
    options.identity_path = "dummy_path";
    options.auto_accept_contacts = true;
    options.auto_reject_calls = true;

    bot_ = std::make_unique<mercury::bot::Bot>(client_, options);
  }

  void TearDown() override {
    if (bot_) bot_->Stop();
    if (client_) client_->Terminate();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::unique_ptr<transport::MockDataTransport> mock_data_transport_impl_;
  transport::MockDataTransport* mock_data_transport_raw_{};
  std::shared_ptr<Client> client_;
  std::unique_ptr<mercury::bot::Bot> bot_;

  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
};

TEST_F(BotApiTest, AutoAcceptContacts) {
  nlohmann::json const req_msg = {
      {"from", peer1.ToString()},
      {"type", "contact_add_request"},
      {"profile", {{"display_name", "Peer One"}}}
  };

  mock_data_transport_raw_->SimulatePeerMessage(peer1, req_msg);
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& msg : mock_log_->sent_signaling_messages) {
          if (msg.value("type", "") == "contact_confirm" && msg.value("to", "") == peer1.ToString()) return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  auto const contact = client_->GetContactService()->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);

  bool confirmed = false;
  for (auto const& msg : mock_log_->sent_signaling_messages) {
    if (msg["type"] == "contact_confirm" && msg["to"] == peer1.ToString()) {
      confirmed = true;
    }
  }
  EXPECT_TRUE(confirmed);
}

TEST_F(BotApiTest, AutoRejectCalls) {
  nlohmann::json const offer_msg =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "media"}, {"sdp", "dummy_sdp_media"}};

  mock_data_transport_raw_->SimulatePeerMessage(peer1, offer_msg);
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, ctx] : mock_log_->closed_connections) {
          if (pid == peer1 && ctx == "media") return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool media_closed = false;
  for (auto const& [pid, ctx] : mock_log_->closed_connections) {
    if (pid == peer1 && ctx == "media") media_closed = true;
  }
  EXPECT_TRUE(media_closed);
}

TEST_F(BotApiTest, MessageHandling) {
  bool message_received = false;
  std::string received_text;

  bot_->OnMessage([&](mercury::bot::Message const& msg) -> void {
    message_received = true;
    received_text = msg.text;
  });

  client_->Connect(peer1);
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return mock_data_transport_raw_->GetPeer(peer1) != nullptr; },
      std::chrono::milliseconds(2000)
  );

  auto peer = mock_data_transport_raw_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  peer->data_channel_is_open = true;
  eillance::crypto::SessionCipher other;
  other.ComputeSharedSecret(peer->session_cipher->GetPublicKey());
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());

  std::string const plain = "Hello Bot";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = plain;

  auto encrypted = other.Encrypt(payload_json.dump());
  nlohmann::json const chat_msg = {
      {"type", "chat"},
      {"message_id", 123},
      {"is_system", false},
      {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
  };

  std::string msg_str = chat_msg.dump();
  std::vector<std::byte> msg_bytes(msg_str.size());
  std::ranges::transform(msg_str, msg_bytes.begin(), [](char c) -> std::byte { return static_cast<std::byte>(c); });

  eillance::EventQueue::Instance().Push(eillance::EventDataChannelMessage{peer1, msg_bytes});

  eillance::EventQueue::Instance().WaitFor([&]() { return message_received; }, std::chrono::milliseconds(2000));

  EXPECT_TRUE(message_received);
  EXPECT_EQ(received_text, "Hello Bot");
}

TEST_F(BotApiTest, SendMessage) {
  client_->AddContact(peer1, std::nullopt);
  client_->Connect(peer1);
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return mock_data_transport_raw_->GetPeer(peer1) != nullptr; },
      std::chrono::milliseconds(2000)
  );

  auto const peer = mock_data_transport_raw_->GetPeer(peer1);
  ASSERT_TRUE(peer);

  peer->data_channel_is_open = true;
  mock_data_transport_raw_->SimulateDataChannelOpen(peer1);
  eillance::EventQueue::Instance().WaitFor(
      [&]() { return peer->data_channel_is_open.load(); },
      std::chrono::milliseconds(2000)
  );

  peer->session_cipher->ComputeSharedSecret(eillance::crypto::SessionCipher().GetPublicKey());

  bot_->SendMessage(peer1.ToString(), "Reply from Bot");
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        std::lock_guard<std::mutex> const lock(mock_log_->mutex_);
        for (auto const& [pid, msg] : mock_log_->sent_peer_messages) {
          if (pid == peer1 && msg.value("type", "") == "chat") {
            return true;
          }
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool sent = false;
  for (auto const& [pid, msg] : mock_log_->sent_peer_messages) {
    if (pid == peer1 && msg.value("type", "") == "chat") {
      sent = true;
    }
  }
  EXPECT_TRUE(sent);
}
