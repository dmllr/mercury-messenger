#include <thread>

#include <gtest/gtest.h>

#include "eillance/events/client_events.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/utils/event_queue.hpp"
#include "mock_persistent_storage.hpp"
using namespace eillance;

class MessageHistoryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    message_history_ = std::make_unique<messaging::MessageHistory>(storage_);
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<messaging::MessageHistory> message_history_;
  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
  ContactID const peer2 = ContactID("0000000000000000000000000000000000000000000000000000000000000002");
};

TEST_F(MessageHistoryTest, AddTextEntry) {
  auto entry = message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "Hello",
       .direction = messaging::MessageDirection::sent,
       .kind = messaging::HistoryEntryKind::textMessage}
  );
  EXPECT_EQ(entry.kind, messaging::HistoryEntryKind::textMessage);
  EXPECT_EQ(std::get<messaging::MessageEntry>(entry.data).content, "Hello");
  EXPECT_EQ(entry.direction, messaging::MessageDirection::sent);
  EXPECT_EQ(entry.status, messaging::MessageStatus::sending);
}

TEST_F(MessageHistoryTest, AddCallEntry) {
  message_history_->AddCallEntry(
      {.peer_id = peer1,
       .kind = messaging::HistoryEntryKind::voiceCall,
       .direction = messaging::CallDirection::incoming,
       .status = messaging::CallStatus::missed,
       .duration_seconds = 0}
  );
  auto page = message_history_->GetHistoryPage(peer1, 0, 1);
  ASSERT_EQ(page.messages.size(), 1);
  auto const& entry = page.messages[0];
  EXPECT_EQ(entry.kind, messaging::HistoryEntryKind::voiceCall);
  auto const& call_data = std::get<messaging::CallEntry>(entry.data);
  EXPECT_EQ(call_data.call_direction, messaging::CallDirection::incoming);
  EXPECT_EQ(call_data.call_status, messaging::CallStatus::missed);
}

TEST_F(MessageHistoryTest, UpdateMessageStatus) {
  ContactID received_peer_id;
  uint64_t received_msg_id = 0;
  auto received_status = messaging::MessageStatus::none;
  bool updated = false;

  auto sub = EventQueue::Instance().Subscribe<EventMessageStatusChanged>([&](auto const& e) {
    received_peer_id = e.peer_id;
    received_msg_id = e.message_id;
    received_status = e.status;
    updated = true;
  });

  auto const entry = message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "Test",
       .direction = messaging::MessageDirection::sent,
       .kind = messaging::HistoryEntryKind::textMessage}
  );
  ASSERT_EQ(entry.status, messaging::MessageStatus::sending);

  message_history_->UpdateMessageStatus(peer1, entry.id, messaging::MessageStatus::delivered);

  EventQueue::Instance().WaitFor([&] { return updated; }, std::chrono::seconds(1));

  EXPECT_EQ(received_peer_id, peer1);
  EXPECT_EQ(received_msg_id, entry.id);
  EXPECT_EQ(received_status, messaging::MessageStatus::delivered);

  EventQueue::Instance().Unsubscribe<EventMessageStatusChanged>(sub);
  auto [messages, total_messages] = message_history_->GetHistoryPage(peer1, 0, 1);
  ASSERT_EQ(messages[0].status, messaging::MessageStatus::delivered);
}

TEST_F(MessageHistoryTest, GetHistoryPage) {
  for (int i = 0; i < 10; ++i) {
    message_history_->AddTextEntry(
        {.peer_id = peer1,
         .content = "Message " + std::to_string(i),
         .direction = messaging::MessageDirection::received,
         .kind = messaging::HistoryEntryKind::textMessage}
    );
  }

  auto page1 = message_history_->GetHistoryPage(peer1, 0, 4);
  EXPECT_EQ(page1.total_messages, 10);
  ASSERT_EQ(page1.messages.size(), 4);
  EXPECT_EQ(std::get<messaging::MessageEntry>(page1.messages[0].data).content, "Message 6");
  EXPECT_EQ(std::get<messaging::MessageEntry>(page1.messages[3].data).content, "Message 9");

  auto page2 = message_history_->GetHistoryPage(peer1, 1, 4);
  EXPECT_EQ(page2.total_messages, 10);
  ASSERT_EQ(page2.messages.size(), 4);
  EXPECT_EQ(std::get<messaging::MessageEntry>(page2.messages[0].data).content, "Message 2");
  EXPECT_EQ(std::get<messaging::MessageEntry>(page2.messages[3].data).content, "Message 5");

  auto last_page = message_history_->GetHistoryPage(peer1, 2, 4);
  EXPECT_EQ(last_page.total_messages, 10);
  ASSERT_EQ(last_page.messages.size(), 2);
  EXPECT_EQ(std::get<messaging::MessageEntry>(last_page.messages[0].data).content, "Message 0");
  EXPECT_EQ(std::get<messaging::MessageEntry>(last_page.messages[1].data).content, "Message 1");
}

TEST_F(MessageHistoryTest, GetChatSummaries) {
  message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "p1m1",
       .direction = messaging::MessageDirection::sent,
       .kind = messaging::HistoryEntryKind::textMessage}
  );
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(10));
  message_history_->AddTextEntry(
      {.peer_id = peer2,
       .content = "p2m1",
       .direction = messaging::MessageDirection::received,
       .kind = messaging::HistoryEntryKind::textMessage}
  );
  message_history_->AddTextEntry(
      {.peer_id = peer2,
       .content = "p2m2",
       .direction = messaging::MessageDirection::received,
       .kind = messaging::HistoryEntryKind::textMessage}
  );  // unread
  eillance::EventQueue::Instance().WaitFor([] { return false; }, std::chrono::milliseconds(10));
  message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "p1m2",
       .direction = messaging::MessageDirection::received,
       .kind = messaging::HistoryEntryKind::textMessage}
  );  // unread

  auto summaries = message_history_->GetChatSummaries({peer1, peer2});
  ASSERT_EQ(summaries.size(), 2);

  // Summaries are sorted by most recent timestamp
  EXPECT_EQ(summaries[0].peer_id, peer1);
  EXPECT_EQ(std::get<messaging::MessageEntry>(summaries[0].last_message.data).content, "p1m2");
  EXPECT_EQ(summaries[0].unread_count, 1);

  EXPECT_EQ(summaries[1].peer_id, peer2);
  EXPECT_EQ(std::get<messaging::MessageEntry>(summaries[1].last_message.data).content, "p2m2");
  EXPECT_EQ(summaries[1].unread_count, 2);
}

TEST_F(MessageHistoryTest, UpdateMessageContent) {
  auto entry = message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "Original",
       .direction = messaging::MessageDirection::sent,
       .kind = messaging::HistoryEntryKind::textMessage}
  );

  message_history_->UpdateMessageContent(peer1, entry.id, "Edited");

  auto loaded = storage_->GetHistoryEntry(peer1, entry.id);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(std::get<messaging::MessageEntry>(loaded->data).content, "Edited");
  EXPECT_TRUE(loaded->is_edited);
}

TEST_F(MessageHistoryTest, MarkMessageDeleted) {
  auto entry = message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "Content",
       .direction = messaging::MessageDirection::sent,
       .kind = messaging::HistoryEntryKind::textMessage}
  );

  message_history_->MarkMessageDeleted(peer1, entry.id);

  auto loaded = storage_->GetHistoryEntry(peer1, entry.id);
  ASSERT_TRUE(loaded);
  EXPECT_TRUE(loaded->is_deleted);
  // Mock clears content, check behavior consistency
  EXPECT_EQ(std::get<messaging::MessageEntry>(loaded->data).content, "");
}

TEST_F(MessageHistoryTest, MarkMessagesAsRead) {
  ContactID received_peer_id;
  uint64_t received_msg_id = 0;
  auto received_status = messaging::MessageStatus::none;
  bool updated = false;

  auto sub = EventQueue::Instance().Subscribe<EventMessageStatusChanged>([&](auto const& e) {
    received_peer_id = e.peer_id;
    received_msg_id = e.message_id;
    received_status = e.status;
    updated = true;
  });

  auto const entry = message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "Test Read",
       .direction = messaging::MessageDirection::received,
       .kind = messaging::HistoryEntryKind::textMessage}
  );

  message_history_->MarkMessagesAsRead(peer1, {entry.id});

  EventQueue::Instance().WaitFor([&] { return updated; }, std::chrono::seconds(1));

  EXPECT_EQ(received_peer_id, peer1);
  EXPECT_EQ(received_msg_id, entry.id);

  EventQueue::Instance().Unsubscribe<EventMessageStatusChanged>(sub);
}

TEST_F(MessageHistoryTest, ToggleReaction) {
  auto entry = message_history_->AddTextEntry(
      {.peer_id = peer1,
       .content = "React to this",
       .direction = messaging::MessageDirection::sent,
       .kind = messaging::HistoryEntryKind::textMessage}
  );

  ContactID const author = peer1;
  message_history_->ToggleReaction(peer1, entry.id, author, messaging::Reaction::thumbsUp);

  auto loaded = storage_->GetHistoryEntry(peer1, entry.id);
  ASSERT_TRUE(loaded);
  ASSERT_EQ(loaded->reactions.size(), 1);
  EXPECT_EQ(loaded->reactions[0].reaction, messaging::Reaction::thumbsUp);
  EXPECT_EQ(loaded->reactions[0].author_id, author);

  // Toggle off
  message_history_->ToggleReaction(peer1, entry.id, author, messaging::Reaction::thumbsUp);
  loaded = storage_->GetHistoryEntry(peer1, entry.id);
  EXPECT_TRUE(loaded->reactions.empty());

  // Toggle different reaction
  message_history_->ToggleReaction(peer1, entry.id, author, messaging::Reaction::heart);
  loaded = storage_->GetHistoryEntry(peer1, entry.id);
  ASSERT_EQ(loaded->reactions.size(), 1);
  EXPECT_EQ(loaded->reactions[0].reaction, messaging::Reaction::heart);
}
