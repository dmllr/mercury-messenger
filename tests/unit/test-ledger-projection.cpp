#include <algorithm>
#include <optional>

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

// --- Security regression tests (audit H4): edit/delete authorship and group/channel takeover ---

class LedgerAuthorizationTest : public LedgerProjectionServiceTest {
 protected:
  void SetUp() override {
    LedgerProjectionServiceTest::SetUp();
    Project(
        TopicID(gid_),
        alice_,
        messaging::LedgerActionType::GroupState,
        {{"type", "group_create"},
         {"name", "Group"},
         {"owner_id", alice_.ToString()},
         {"members",
          {{{"contact_id", alice_.ToString()}, {"role", 1}},
           {{"contact_id", bob_.ToString()}, {"role", 0}},
           {{"contact_id", client_->GetId().ToString()}, {"role", 0}}}}}
    );
    Project(
        TopicID(gid_),
        alice_,
        messaging::LedgerActionType::Message,
        {{"type", "chat"}, {"message_id", msg_id_}, {"payload", {{"c", "original"}, {"v", 2}}}}
    );
  }

  void Project(TopicID const& topic, ContactID const& author, messaging::LedgerActionType type, nlohmann::json const& j) {
    messaging::LedgerEntry entry;
    entry.topic_id = topic;
    entry.author_id = author;
    entry.created_at = Timestamp{++ts_};
    entry.type = type;
    entry.payload = j.dump();
    EventQueue::Instance().Push(EventLedgerAppended{entry, true});
    EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(100));
  }

  [[nodiscard]] auto Message() const -> std::optional<messaging::HistoryEntryRecord> {
    return storage_->GetHistoryEntry(TopicID(gid_), msg_id_);
  }

  [[nodiscard]] auto Content() const -> std::string {
    auto const rec = Message();
    if (!rec) return {};
    auto const* content = messaging::GetMessageContent(rec->data);
    return content ? content->content : std::string{};
  }

  ContactID const alice_{"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"};
  ContactID const bob_{"aa4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e99"};
  GroupID const gid_{"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15"};
  std::uint64_t const msg_id_ = 4242;
  std::uint64_t ts_ = 1000;
};

TEST_F(LedgerAuthorizationTest, NonAuthorCannotEditMessage) {
  ASSERT_EQ(Content(), "original");
  Project(
      TopicID(gid_),
      bob_,
      messaging::LedgerActionType::MessageEdit,
      {{"type", "message_edit"}, {"target_id", msg_id_}, {"content", "forged"}}
  );
  EXPECT_EQ(Content(), "original");
}

TEST_F(LedgerAuthorizationTest, AuthorCanEditMessage) {
  Project(
      TopicID(gid_),
      alice_,
      messaging::LedgerActionType::MessageEdit,
      {{"type", "message_edit"}, {"target_id", msg_id_}, {"content", "edited"}}
  );
  EXPECT_EQ(Content(), "edited");
}

TEST_F(LedgerAuthorizationTest, NonAuthorCannotDeleteMessage) {
  Project(
      TopicID(gid_),
      bob_,
      messaging::LedgerActionType::MessageDelete,
      {{"type", "message_delete"}, {"target_id", msg_id_}}
  );
  ASSERT_TRUE(Message().has_value());
  EXPECT_FALSE(Message()->is_deleted);
}

TEST_F(LedgerAuthorizationTest, AuthorCanDeleteMessage) {
  Project(
      TopicID(gid_),
      alice_,
      messaging::LedgerActionType::MessageDelete,
      {{"type", "message_delete"}, {"target_id", msg_id_}}
  );
  ASSERT_TRUE(Message().has_value());
  EXPECT_TRUE(Message()->is_deleted);
}

TEST_F(LedgerAuthorizationTest, GroupCreateCannotTakeOverExistingGroup) {
  Project(
      TopicID(gid_),
      bob_,
      messaging::LedgerActionType::GroupState,
      {{"type", "group_create"},
       {"name", "Hijacked"},
       {"owner_id", bob_.ToString()},
       {"members", {{{"contact_id", bob_.ToString()}, {"role", 1}}}}}
  );
  auto const groups = client_->GetGroups();
  auto const it = std::ranges::find_if(groups, [&](auto const& g) { return g.id == gid_; });
  ASSERT_NE(it, groups.end());
  EXPECT_EQ(it->owner_id, alice_);
  EXPECT_EQ(it->name, "Group");
}

TEST_F(LedgerAuthorizationTest, GroupCreateOwnerMustBeAuthor) {
  GroupID const other("cc4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e22");
  Project(
      TopicID(other),
      bob_,
      messaging::LedgerActionType::GroupState,
      {{"type", "group_create"},
       {"name", "Spoofed owner"},
       {"owner_id", alice_.ToString()},
       {"members", {{{"contact_id", bob_.ToString()}, {"role", 1}}}}}
  );
  auto const groups = client_->GetGroups();
  EXPECT_TRUE(std::ranges::none_of(groups, [&](auto const& g) { return g.id == other; }));
}

TEST_F(LedgerAuthorizationTest, ChannelStubIsCompletedOnlyByOwner) {
  ChannelID const cid("dd4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e33");
  client_->SubscribeToChannel(cid, alice_);

  Project(
      TopicID(cid),
      bob_,
      messaging::LedgerActionType::ChannelState,
      {{"type", "channel_create"}, {"name", "Hijacked"}, {"description", ""}, {"owner_id", bob_.ToString()}}
  );
  auto find = [&] {
    auto const channels = client_->GetChannels();
    auto const it = std::ranges::find_if(channels, [&](auto const& c) { return c.id == cid; });
    return it != channels.end() ? std::optional<identity::Channel>(*it) : std::nullopt;
  };
  ASSERT_TRUE(find().has_value());
  EXPECT_EQ(find()->owner_id, alice_);
  EXPECT_NE(find()->name, "Hijacked");

  Project(
      TopicID(cid),
      alice_,
      messaging::LedgerActionType::ChannelState,
      {{"type", "channel_create"}, {"name", "Real"}, {"description", ""}, {"owner_id", alice_.ToString()}}
  );
  ASSERT_TRUE(find().has_value());
  EXPECT_EQ(find()->name, "Real");
}
