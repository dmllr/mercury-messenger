#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/services/ledger_projection_service.hpp"
#include "mock_data_transport.hpp"
#include "mock_media_transport.hpp"
#include "mock_persistent_storage.hpp"

using namespace eillance;

class LedgerProjectionServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto storage_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_ = storage_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(storage_impl));

    mock_log_ = std::make_shared<transport::MockCallLog>();
    auto media_transport =
        std::make_shared<transport::MediaTransport>(std::make_unique<transport::MockMediaTransport>(mock_log_));
    auto data_transport = std::make_shared<transport::DataTransport>(std::make_unique<transport::MockDataTransport>());

    client_ = std::make_shared<Client>(storage_, media_transport, data_transport);
    std::ignore = client_->GenerateNewIdentity();
    client_->Start();
  }

  void TearDown() override {
    client_->Terminate();
    client_.reset();
  }

  storage::MockPersistentStorageImpl* mock_storage_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::MockCallLog> mock_log_;
  std::shared_ptr<Client> client_;
};

TEST_F(LedgerProjectionServiceTest, ProjectMessage) {
  ContactID const peer1("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");

  nlohmann::json const payload_json = {
      {"type", "chat"},
      {"message_id", 123},
      {"is_system", false},
      {"payload", {{"c", "Projected Message"}, {"v", 2}}}
  };

  messaging::LedgerEntry entry;
  entry.topic_id = TopicID(peer1);
  entry.author_id = peer1;
  entry.type = messaging::LedgerActionType::Message;
  entry.payload = payload_json.dump();

  EventQueue::Instance().Push(EventLedgerAppended{entry});

  EventQueue::Instance().WaitFor(
      [&]() {
        auto [messages, total] = client_->GetMessageHistoryPage(TopicID(peer1), 0, 1);
        return !messages.empty();
      },
      std::chrono::seconds(2)
  );

  auto [messages, total] = client_->GetMessageHistoryPage(TopicID(peer1), 0, 10);
  bool found = false;
  for (auto const& msg : messages) {
    if (std::holds_alternative<messaging::ChatMessageEntry>(msg.data)) {
      if (std::get<messaging::ChatMessageEntry>(msg.data).message.content == "Projected Message") {
        found = true;
      }
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(LedgerProjectionServiceTest, ProjectGroupState) {
  ContactID const peer1("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  GroupID const gid("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15");
  nlohmann::json const payload_json = {
      {"type", "group_create"},
      {"name", "New Group"},
      {"owner_id", peer1.ToString()},
      {"members", {{{"contact_id", peer1.ToString()}, {"role", 1}}}}
  };

  messaging::LedgerEntry entry;
  entry.topic_id = TopicID(gid);
  entry.author_id = peer1;
  entry.type = messaging::LedgerActionType::GroupState;
  entry.payload = payload_json.dump();

  EventQueue::Instance().Push(EventLedgerAppended{entry});

  EventQueue::Instance().WaitFor(
      [&]() {
        auto members = client_->GetGroupMembers(gid);
        return !members.empty();
      },
      std::chrono::seconds(2)
  );

  auto members = client_->GetGroupMembers(gid);
  ASSERT_EQ(members.size(), 1);
  EXPECT_EQ(members[0].contact_id, peer1);
}

TEST_F(LedgerProjectionServiceTest, ProjectChannelState) {
  ContactID const peer1("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  ChannelID const cid("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15");
  nlohmann::json const payload_json = {
      {"type", "channel_create"},
      {"name", "New Channel"},
      {"description", "A description"},
      {"owner_id", peer1.ToString()}
  };

  messaging::LedgerEntry entry;
  entry.topic_id = TopicID(cid);
  entry.author_id = peer1;
  entry.type = messaging::LedgerActionType::ChannelState;
  entry.payload = payload_json.dump();

  EventQueue::Instance().Push(EventLedgerAppended{entry});

  EventQueue::Instance().WaitFor(
      [&]() {
        auto channels = client_->GetChannels();
        return !channels.empty();
      },
      std::chrono::seconds(2)
  );

  auto channels = client_->GetChannels();
  ASSERT_EQ(channels.size(), 1);
  EXPECT_EQ(channels[0].id, cid);
}
