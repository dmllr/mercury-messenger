#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/capture/config.hpp"
#include "eillance/client/client.hpp"
#include "eillance/utils/packet.hpp"
#include "mercury/bot/bot.hpp"
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

    client_ = std::make_shared<Client>(storage, media_transport);

    (void)client_->Account()->GenerateNewIdentity();

    client_->Start();

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
  std::shared_ptr<Client> client_;
  std::unique_ptr<mercury::bot::Bot> bot_;

  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
};

TEST_F(BotApiTest, AutoAcceptContacts) {
  nlohmann::json const req_msg = {{"type", "contact_add_request"}, {"profile", {{"display_name", "Peer One"}}}};

  storage::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = storage::LedgerActionType::ContactState;
  entry.payload = req_msg.dump();

  eillance::EventQueue::Instance().Push(EventLedgerAppended{entry});
  eillance::EventQueue::Instance().WaitFor(
      [&]() {
        auto entries = mock_storage_->GetLedgerEntries(TopicID(peer1));
        for (auto const& msg : entries) {
          if (msg.payload.find("contact_confirm") != std::string::npos) return true;
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  auto const contact = client_->Contacts()->GetContact(peer1);
  ASSERT_TRUE(contact.has_value());
  EXPECT_EQ(contact->status, identity::ContactStatus::confirmed);

  bool confirmed = false;
  for (auto const& msg : mock_storage_->GetLedgerEntries(TopicID(peer1))) {
    if (msg.payload.find("contact_confirm") != std::string::npos) {
      confirmed = true;
    }
  }
  EXPECT_TRUE(confirmed);
}

TEST_F(BotApiTest, AutoRejectCalls) {
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

  storage::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = storage::LedgerActionType::Message;
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
