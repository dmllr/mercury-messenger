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

class BotApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    auto storage = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();

    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));

    auto mock_dt = std::make_unique<transport::MockDataTransport>();
    mock_dt_ = mock_dt.get();
    auto data_transport = std::make_shared<transport::DataTransport>(std::move(mock_dt));
    client_ = std::make_shared<Client>(storage, media_transport, data_transport);

    (void)client_->Account()->GenerateNewIdentity();

    client_->Start();

    std::string mnemonic;
    peer_ident_ = eillance::crypto::Identity::CreateNew(mnemonic);
    peer1 = ContactID(peer_ident_->GetPublicKey());

    mercury::bot::Bot::Options options;
    options.identity_path = "dummy_path";
    options.auto_accept_contacts = true;
    options.auto_reject_calls = true;

    bot_ = std::make_unique<mercury::bot::Bot>(client_, options);
    bot_->Start();
  }

  void TearDown() override {
    if (bot_) bot_->Stop();
    if (client_) client_->Terminate();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<transport::MockCallLog> mock_log_;
  transport::MockDataTransport* mock_dt_{};
  std::shared_ptr<Client> client_;
  std::unique_ptr<mercury::bot::Bot> bot_;

  std::unique_ptr<crypto::Identity> peer_ident_;
  ContactID peer1;
};

TEST_F(BotApiTest, AutoAcceptContacts) {
  nlohmann::json const req_msg = {{"type", "contact_add_request"}, {"profile", {{"display_name", "Peer One"}}}};

  messaging::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = messaging::LedgerActionType::ContactState;
  entry.payload = req_msg.dump();

  eillance::EventQueue::Instance().Push(EventLedgerAppended{entry});
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto contact = client_->Contacts()->GetContact(peer1);
        return contact && contact->status == identity::ContactStatus::confirmed;
      },
      std::chrono::milliseconds(2000)
  );

  auto const contact = client_->Contacts()->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);
}

TEST_F(BotApiTest, AutoRejectCalls) {
  client_->Connect(peer1);
  if (auto p = mock_dt_->GetPeer(peer1)) {
    p->crcp_verified = true;
    p->data_channel_is_open = true;
  }

  auto sig = peer_ident_->Sign(client_->GetId().Span());
  mock_storage_->UpdateContact(
      {.peer_id = peer1, .status = identity::ContactStatus::confirmed, .signature_from_them = sig}
  );
  client_->Contacts()->ReloadCache();

  nlohmann::json const offer_msg =
      {{"from", peer1.ToString()}, {"type", "offer"}, {"context", "media"}, {"sdp", "dummy_sdp_media"}};

  std::string const str = offer_msg.dump();
  std::vector<std::byte> bytes(str.size());
  std::memcpy(bytes.data(), str.data(), str.size());
  eillance::EventQueue::Instance().Push(EventDataChannelMessage{peer1, bytes});

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
  std::ignore = client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  std::atomic<bool> message_received{false};
  std::string received_text;

  bot_->OnMessage([&](mercury::bot::Message const& msg) -> void {
    received_text = msg.text;
    message_received = true;
  });

  // Simulate connected data channel internally to allow processing
  eillance::EventQueue::Instance().Push(EventDataChannelOpen{peer1});
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(100));

  std::string const plain = "Hello Bot";
  nlohmann::json payload_json;
  payload_json[services::MessagingService::keyVersion] = services::MessagingService::protocolVersion;
  payload_json[services::MessagingService::keyContent] = plain;

  nlohmann::json const chat_msg =
      {{"type", "chat"}, {"message_id", 123}, {"is_system", false}, {"payload", payload_json}};

  messaging::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = messaging::LedgerActionType::Message;
  entry.payload = chat_msg.dump();

  EventQueue::Instance().Push(EventLedgerAppended{entry});

  EventQueue::Instance().WaitFor([&]() { return message_received.load(); }, std::chrono::milliseconds(2000));

  EXPECT_TRUE(message_received);
  EXPECT_EQ(received_text, "Hello Bot");
}

TEST_F(BotApiTest, SendMessage) {
  std::ignore = client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  bot_->SendMessage(peer1.ToString(), "Reply from Bot");
  EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = mock_storage_->GetLedgerEntries(TopicID(peer1));
        for (auto const& msg : entries) {
          if (msg.payload.find("Reply from Bot") != std::string::npos) return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool sent = false;
  for (auto const& msg : mock_storage_->GetLedgerEntries(TopicID(peer1))) {
    if (msg.payload.find("Reply from Bot") != std::string::npos) {
      sent = true;
    }
  }
  EXPECT_TRUE(sent);
}

TEST_F(BotApiTest, MarkAsRead) {
  std::ignore = client_->AddContact(peer1, std::nullopt);
  mock_storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  client_->Contacts()->ReloadCache();

  // Ensure data channel open
  eillance::EventQueue::Instance().Push(EventDataChannelOpen{peer1});
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(100));

  // Simulate bot sending a message
  bot_->MarkAsRead(peer1.ToString(), 999);

  EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = mock_storage_->GetLedgerEntries(TopicID(peer1));
        for (auto const& msg : entries) {
          if (msg.payload.find("message_read") != std::string::npos) return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  bool marked = false;
  for (auto const& msg : mock_storage_->GetLedgerEntries(TopicID(peer1))) {
    if (msg.payload.find("message_read") != std::string::npos) {
      marked = true;
    }
  }
  EXPECT_TRUE(marked);
}
