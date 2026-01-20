#include <algorithm>
#include <filesystem>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"

// Use the eillance namespace for convenience in the test file.
using namespace eillance;

class SQLiteStorageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Generate a unique path for the database file for each test.
    // This ensures test isolation.
    ::testing::TestInfo const* const test_info = ::testing::UnitTest::GetInstance()->current_test_info();
    std::string filename = std::string(test_info->test_suite_name()) + "_" + std::string(test_info->name()) + ".db";
    db_path_ = std::filesystem::temp_directory_path() / "eillance_tests" / filename;

    // Ensure the directory exists and the file does not exist from a previous failed run.
    std::filesystem::create_directories(db_path_.parent_path());
    if (std::filesystem::exists(db_path_)) {
      std::filesystem::remove(db_path_);
    }

    // Initialize the storage implementation.
    storage_ = std::make_unique<storage::SQLiteStorage>(db_path_);
  }

  void TearDown() override {
    // The unique_ptr's destruction will close the database connection.
    storage_.reset();

    // Clean up the database file after the test.
    if (std::filesystem::exists(db_path_)) {
      std::filesystem::remove(db_path_);
    }
  }

  std::filesystem::path db_path_;
  std::unique_ptr<storage::SQLiteStorage> storage_;
  ContactID const peer1 = ContactID("0000000000000000000000000000000000000000000000000000000000000001");
  ContactID const peer2 = ContactID("0000000000000000000000000000000000000000000000000000000000000002");
};

// Test suite for settings management
TEST_F(SQLiteStorageTest, Settings) {
  // Test Set and Get
  storage_->SetSetting("test_key", "test_value");
  auto value = storage_->GetSetting("test_key");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "test_value");

  // Test Get non-existent
  auto non_existent = storage_->GetSetting("non_existent_key");
  EXPECT_FALSE(non_existent.has_value());

  // Test Update
  storage_->SetSetting("test_key", "new_value");
  value = storage_->GetSetting("test_key");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, "new_value");

  // Test Delete
  storage_->DeleteSetting("test_key");
  auto deleted_value = storage_->GetSetting("test_key");
  EXPECT_FALSE(deleted_value.has_value());
}

// Test suite for raw byte storage
TEST_F(SQLiteStorageTest, RawKeyValue) {
  std::vector<std::byte> raw_data;
  for (int i = 0; i < 256; ++i) {
    raw_data.push_back(static_cast<std::byte>(i));
  }

  // Test SetRaw and GetRaw
  ASSERT_TRUE(storage_->SetRaw("raw_key", raw_data));
  auto retrieved_data = storage_->GetRaw("raw_key");

  ASSERT_TRUE(retrieved_data.has_value());
  EXPECT_EQ(*retrieved_data, raw_data);

  // Test with an empty vector
  std::vector<std::byte> empty_data;
  ASSERT_TRUE(storage_->SetRaw("empty_raw_key", empty_data));
  auto retrieved_empty = storage_->GetRaw("empty_raw_key");
  ASSERT_TRUE(retrieved_empty.has_value());
  EXPECT_EQ(*retrieved_empty, empty_data);
}

// Test suite for contact management
TEST_F(SQLiteStorageTest, Contacts) {
  identity::Contact contact1 = {peer1, identity::ContactStatus::pendingApproval};
  identity::Contact contact2 = {peer2, identity::ContactStatus::confirmed};

  // Test Add/Update
  storage_->UpdateContact(contact1);
  auto retrieved_contact1 = storage_->GetContact(peer1);
  ASSERT_TRUE(retrieved_contact1.has_value());
  EXPECT_EQ(retrieved_contact1->peer_id, contact1.peer_id);
  EXPECT_EQ(retrieved_contact1->status, contact1.status);

  // Test Get non-existent
  auto non_existent =
      storage_->GetContact(ContactID("9999999999999999999999999999999999999999999999999999999999999999"));
  EXPECT_FALSE(non_existent.has_value());

  // Test Update status
  contact1.status = identity::ContactStatus::confirmed;
  storage_->UpdateContact(contact1);
  retrieved_contact1 = storage_->GetContact(peer1);
  ASSERT_TRUE(retrieved_contact1.has_value());
  EXPECT_EQ(retrieved_contact1->status, identity::ContactStatus::confirmed);

  // Test Get All
  storage_->UpdateContact(contact2);
  auto all_contacts = storage_->GetContacts();
  ASSERT_EQ(all_contacts.size(), 2);
  // Find our contacts in the list (order is not guaranteed)
  bool found1 = false, found2 = false;
  for (auto const& c : all_contacts) {
    if (c.peer_id == peer1) {
      EXPECT_EQ(c.status, identity::ContactStatus::confirmed);
      found1 = true;
    } else if (c.peer_id == peer2) {
      EXPECT_EQ(c.status, identity::ContactStatus::confirmed);
      found2 = true;
    }
  }
  EXPECT_TRUE(found1 && found2);

  // Test Remove
  storage_->RemoveContact(peer1);
  EXPECT_FALSE(storage_->GetContact(peer1).has_value());
  all_contacts = storage_->GetContacts();
  EXPECT_EQ(all_contacts.size(), 1);
  EXPECT_EQ(all_contacts[0].peer_id, peer2);
}

// Test suite for history management
TEST_F(SQLiteStorageTest, History) {
  // Ensure contact exists for FK constraint
  identity::Contact contact{peer1, identity::ContactStatus::confirmed};
  storage_->UpdateContact(contact);

  // Add a text message
  messaging::HistoryEntryRecord text_entry{
      .kind = messaging::HistoryEntryKind::textMessage,
      .timestamp = Time::Now(),
      .direction = messaging::MessageDirection::sent,
      .status = messaging::MessageStatus::sending,
      .data = messaging::MessageEntry{"Hello"},
      .peer_id = peer1
  };
  auto added_text_entry = storage_->AddHistoryEntry(text_entry);
  EXPECT_NE(added_text_entry.id, 0);  // ID should be assigned by DB

  // Add a call record
  messaging::HistoryEntryRecord call_entry{
      .kind = messaging::HistoryEntryKind::voiceCall,
      .timestamp = Time::Now(),
      .direction = messaging::MessageDirection::received,
      .data = messaging::CallEntry{30, messaging::CallDirection::incoming, messaging::CallStatus::answered},
      .peer_id = peer1
  };
  auto added_call_entry = storage_->AddHistoryEntry(call_entry);
  EXPECT_NE(added_call_entry.id, 0);

  // Test GetHistoryPage
  auto page = storage_->GetHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page.messages.size(), 2);
  EXPECT_EQ(page.total_messages, 2);
  EXPECT_EQ(page.messages[0].id, added_text_entry.id);
  EXPECT_EQ(std::get<messaging::MessageEntry>(page.messages[0].data).content, "Hello");
  EXPECT_EQ(page.messages[1].id, added_call_entry.id);
  EXPECT_EQ(std::get<messaging::CallEntry>(page.messages[1].data).duration_seconds, 30);

  // Test UpdateMessageStatus
  storage_->UpdateMessageStatus(peer1, added_text_entry.id, messaging::MessageStatus::delivered);
  page = storage_->GetHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page.messages.size(), 2);
  EXPECT_EQ(page.messages[0].status, messaging::MessageStatus::delivered);

  // Test GetMessagesWithStatus
  auto sent_messages = storage_->GetMessagesWithStatus(peer1, messaging::MessageStatus::sending);
  EXPECT_EQ(sent_messages.size(), 0);
  auto delivered_messages = storage_->GetMessagesWithStatus(peer1, messaging::MessageStatus::delivered);
  ASSERT_EQ(delivered_messages.size(), 1);
  EXPECT_EQ(delivered_messages[0].id, added_text_entry.id);

  // Test UpdateMessageStatuses
  auto text_entry2 = storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .status = messaging::MessageStatus::sending,
       .data = messaging::MessageEntry{"World"},
       .peer_id = peer1}
  );
  storage_->UpdateMessageStatuses(peer1, {added_text_entry.id, text_entry2.id}, messaging::MessageStatus::read);

  auto read_messages = storage_->GetMessagesWithStatus(peer1, messaging::MessageStatus::read);
  ASSERT_EQ(read_messages.size(), 2);
}

TEST_F(SQLiteStorageTest, HistoryPagination) {
  identity::Contact contact{peer1, identity::ContactStatus::confirmed};
  storage_->UpdateContact(contact);

  constexpr int total_messages = 25;
  uint64_t newest_id = 0;

  for (int i = 0; i < total_messages; ++i) {
    auto entry = storage_->AddHistoryEntry(
        {.kind = messaging::HistoryEntryKind::textMessage,
         .timestamp = static_cast<uint64_t>(Time::Now() + i),  // ensure unique timestamps for ordering
         .direction = messaging::MessageDirection::received,
         .data = messaging::MessageEntry{"Message " + std::to_string(i)},
         .peer_id = peer1}
    );
    if (i == total_messages - 1) {
      newest_id = entry.id;
    }
  }

  // The implementation fetches pages of newest messages first for "load older" functionality.
  // Page 0 should contain the 10 newest messages, ordered ascendingly.
  auto page0 = storage_->GetHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page0.messages.size(), 10);
  EXPECT_EQ(page0.total_messages, total_messages);
  EXPECT_EQ(std::get<messaging::MessageEntry>(page0.messages[0].data).content, "Message 15");
  EXPECT_EQ(std::get<messaging::MessageEntry>(page0.messages[9].data).content, "Message 24");
  EXPECT_EQ(page0.messages[9].id, newest_id);

  // Page 1 should contain the next 10 older messages
  auto page1 = storage_->GetHistoryPage(peer1, 1, 10);
  ASSERT_EQ(page1.messages.size(), 10);
  EXPECT_EQ(page1.total_messages, total_messages);
  EXPECT_EQ(std::get<messaging::MessageEntry>(page1.messages[0].data).content, "Message 5");
  EXPECT_EQ(std::get<messaging::MessageEntry>(page1.messages[9].data).content, "Message 14");

  // Page 2 should contain the last 5 oldest messages
  auto page2 = storage_->GetHistoryPage(peer1, 2, 10);
  ASSERT_EQ(page2.messages.size(), 5);
  EXPECT_EQ(page2.total_messages, total_messages);
  EXPECT_EQ(std::get<messaging::MessageEntry>(page2.messages[0].data).content, "Message 0");
  EXPECT_EQ(std::get<messaging::MessageEntry>(page2.messages[4].data).content, "Message 4");

  // Test out of bounds page
  auto page3 = storage_->GetHistoryPage(peer1, 3, 10);
  EXPECT_EQ(page3.messages.size(), 0);
  EXPECT_EQ(page3.total_messages, total_messages);
}

TEST_F(SQLiteStorageTest, ChatSummaries) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  storage_->UpdateContact({peer2, identity::ContactStatus::confirmed});

  ContactID peer3_no_history = ContactID("0000000000000000000000000000000000000000000000000000000000000003");

  auto const base_time = Time::Now();

  // History for peer1
  storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 100,
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::none,
       .data = messaging::MessageEntry{"p1m1"},
       .peer_id = peer1}
  );
  storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 200,
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::none,
       .data = messaging::MessageEntry{"p1m2"},
       .peer_id = peer1}
  );
  storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 300,
       .direction = messaging::MessageDirection::sent,
       .status = messaging::MessageStatus::sent,
       .data = messaging::MessageEntry{"p1m3"},
       .peer_id = peer1}
  );

  // History for peer2
  storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 50,
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::read,
       .data = messaging::MessageEntry{"p2m1"},
       .peer_id = peer2}
  );
  auto p2_last = storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::voiceCall,
       .timestamp = base_time + 150,
       .direction = messaging::MessageDirection::received,
       .data = messaging::CallEntry{0, messaging::CallDirection::incoming, messaging::CallStatus::missed},
       .peer_id = peer2}
  );

  auto summaries = storage_->GetChatSummaries({peer1, peer2, peer3_no_history});

  // Summaries are sorted by timestamp of last message, descending.
  ASSERT_EQ(summaries.size(), 2);  // peer3 has no history

  // First summary should be peer1
  EXPECT_EQ(summaries[0].peer_id, peer1);
  EXPECT_EQ(std::get<messaging::MessageEntry>(summaries[0].last_message.data).content, "p1m3");
  EXPECT_EQ(summaries[0].unread_count, 2);  // p1m1 and p1m2 are received and not read

  // Second summary should be peer2
  EXPECT_EQ(summaries[1].peer_id, peer2);
  EXPECT_EQ(summaries[1].last_message.id, p2_last.id);
  EXPECT_EQ(summaries[1].last_message.kind, messaging::HistoryEntryKind::voiceCall);
  EXPECT_EQ(summaries[1].unread_count, 0);  // The one text message is read, and calls don't count as unread.
}

TEST_F(SQLiteStorageTest, UpdateHistoryEntry) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});

  auto entry = storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .status = messaging::MessageStatus::sending,
       .data = messaging::MessageEntry{"Initial content"},
       .peer_id = peer1}
  );

  entry.status = messaging::MessageStatus::read;
  entry.data = messaging::MessageEntry{"Updated content"};
  storage_->UpdateHistoryEntry(entry);

  auto page = storage_->GetHistoryPage(peer1, 0, 1);
  ASSERT_EQ(page.messages.size(), 1);
  EXPECT_EQ(page.messages[0].id, entry.id);
  EXPECT_EQ(page.messages[0].status, messaging::MessageStatus::read);
  EXPECT_EQ(std::get<messaging::MessageEntry>(page.messages[0].data).content, "Updated content");
}

TEST_F(SQLiteStorageTest, LargeMessages) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});

  std::string long_message(3000, 'a');

  auto entry = storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .status = messaging::MessageStatus::sending,
       .data = messaging::MessageEntry{long_message},
       .peer_id = peer1}
  );

  EXPECT_TRUE(entry.is_truncated);

  auto page = storage_->GetHistoryPage(peer1, 0, 1);
  ASSERT_EQ(page.messages.size(), 1);
  EXPECT_TRUE(page.messages[0].is_truncated);
  EXPECT_LT(std::get<messaging::MessageEntry>(page.messages[0].data).content.length(), long_message.length());

  auto full_content = storage_->GetFullMessageContent(entry.id);
  ASSERT_TRUE(full_content.has_value());
  EXPECT_EQ(*full_content, long_message);
}
