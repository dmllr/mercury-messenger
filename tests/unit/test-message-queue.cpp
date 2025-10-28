#include "eillance/messaging/message_queue.hpp"
#include "mock_persistent_storage.hpp"

#include <gtest/gtest.h>

using namespace eillance;

class MessageQueueTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto mock_impl = std::make_unique<storage::MockPersistentStorageImpl>();
    mock_storage_impl_ = mock_impl.get();
    storage_ = std::make_shared<storage::PersistentStorage>(std::move(mock_impl));
    message_queue_ = std::make_unique<messaging::MessageQueue>(storage_);
  }

  storage::MockPersistentStorageImpl* mock_storage_impl_{};
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::unique_ptr<messaging::MessageQueue> message_queue_;
  const std::string peer1 = "peer1";
  const std::string peer2 = "peer2";
};

TEST_F(MessageQueueTest, EnqueueAndGet) {
  message_queue_->Enqueue(peer1, 1, "Hello");
  message_queue_->Enqueue(peer1, 2, "World");
  message_queue_->Enqueue(peer2, 3, "Hi");

  auto peer1_messages = message_queue_->GetQueuedMessages(peer1);
  ASSERT_EQ(peer1_messages.size(), 2);
  EXPECT_EQ(peer1_messages[0].id, 1);
  EXPECT_EQ(peer1_messages[0].content, "Hello");
  EXPECT_EQ(peer1_messages[1].id, 2);
  EXPECT_EQ(peer1_messages[1].content, "World");

  auto peer2_messages = message_queue_->GetQueuedMessages(peer2);
  ASSERT_EQ(peer2_messages.size(), 1);
  EXPECT_EQ(peer2_messages[0].id, 3);
  EXPECT_EQ(peer2_messages[0].content, "Hi");
}

TEST_F(MessageQueueTest, GetEmptyQueue) {
  auto messages = message_queue_->GetQueuedMessages(peer1);
  EXPECT_TRUE(messages.empty());
}

TEST_F(MessageQueueTest, Clear) {
  message_queue_->Enqueue(peer1, 1, "Message");
  ASSERT_FALSE(message_queue_->GetQueuedMessages(peer1).empty());

  message_queue_->Clear(peer1);
  EXPECT_TRUE(message_queue_->GetQueuedMessages(peer1).empty());
}

TEST_F(MessageQueueTest, Persistence) {
  message_queue_->Enqueue(peer1, 101, "Persistent message");

  // Create a new queue with the same storage
  auto new_queue = std::make_unique<messaging::MessageQueue>(storage_);

  auto messages = new_queue->GetQueuedMessages(peer1);
  ASSERT_EQ(messages.size(), 1);
  EXPECT_EQ(messages[0].id, 101);
  EXPECT_EQ(messages[0].content, "Persistent message");

  // Clearing in new queue should persist
  new_queue->Clear(peer1);
  auto another_new_queue = std::make_unique<messaging::MessageQueue>(storage_);
  EXPECT_TRUE(another_new_queue->GetQueuedMessages(peer1).empty());
}
