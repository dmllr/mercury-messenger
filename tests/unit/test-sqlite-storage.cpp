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
    std::string const filename =
        std::string(test_info->test_suite_name()) + "_" + std::string(test_info->name()) + ".db";
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
  ContactID const peer1 = ContactID("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  ContactID const peer2 = ContactID("3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968ce0eb0f6226252e15");
  uint64_t next_id_ = 1;

  messaging::HistoryEntryRecord AddTestEntry(messaging::HistoryEntryRecord r) {
    if (r.id == 0) r.id = next_id_++;
    return storage_->AddHistoryEntry(r);
  }
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
  raw_data.reserve(256);
  for (int i = 0; i < 256; ++i) {
    raw_data.push_back(static_cast<std::byte>(i));
  }

  // Test SetRaw and GetRaw
  ASSERT_TRUE(storage_->SetRaw("raw_key", raw_data));
  auto retrieved_data = storage_->GetRaw("raw_key");

  ASSERT_TRUE(retrieved_data.has_value());
  EXPECT_EQ(*retrieved_data, raw_data);

  // Test with an empty vector
  std::vector<std::byte> const empty_data;
  ASSERT_TRUE(storage_->SetRaw("empty_raw_key", empty_data));
  auto retrieved_empty = storage_->GetRaw("empty_raw_key");
  ASSERT_TRUE(retrieved_empty.has_value());
  EXPECT_EQ(*retrieved_empty, empty_data);
}

// Test suite for contact management
TEST_F(SQLiteStorageTest, Contacts) {
  identity::Contact contact1 = {peer1, identity::ContactStatus::pendingApproval};
  identity::Contact const contact2 = {peer2, identity::ContactStatus::confirmed};

  // Test Add/Update
  storage_->UpdateContact(contact1);
  auto retrieved_contact1 = storage_->GetContact(peer1);
  ASSERT_TRUE(retrieved_contact1.has_value());
  EXPECT_EQ(retrieved_contact1->peer_id, contact1.peer_id);
  EXPECT_EQ(retrieved_contact1->status, contact1.status);

  // Test Get non-existent
  auto non_existent =
      storage_->GetContact(ContactID("061e8db172f02b3df4735fb291244e6ea60cc6e3ef31e42ceab9cf531189410d"));
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
  bool found1 = false;
  bool found2 = false;
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

// Test suite for ledger sync
TEST_F(SQLiteStorageTest, SyncLedger) {
  TopicID topic(peer1);
  storage_->AddLedgerEntry(
      {.seq_id = 1,
       .topic_id = topic,
       .author_id = peer1,
       .created_at = Timestamp{100},
       .type = messaging::LedgerActionType::Message,
       .payload = "data",
       .prev_hash = messaging::LedgerHash(),
       .hash = messaging::LedgerHash(std::string(64, '1'))}
  );

  auto latest = storage_->GetLatestLedgerEntry(topic);
  ASSERT_TRUE(latest);
  EXPECT_EQ(latest->hash, messaging::LedgerHash(std::string(64, '1')));

  auto recent = storage_->GetRecentLedgerHashes(topic);
  ASSERT_FALSE(recent.empty());
  EXPECT_EQ(recent[0], messaging::LedgerHash(std::string(64, '1')));
}

TEST_F(SQLiteStorageTest, SyncRequests) {
  TopicID topic(peer1);
  storage_->AddSyncRequest({topic, peer2, Timestamp{100}, Timestamp{0}});

  auto reqs = storage_->GetSyncRequests();
  ASSERT_EQ(reqs.size(), 1);
  EXPECT_EQ(reqs[0].topic_id, topic);
  EXPECT_EQ(reqs[0].target_peer_id, peer2);

  storage_->UpdateSyncRequestAttempt(topic, peer2, Timestamp{200});
  reqs = storage_->GetSyncRequests();
  EXPECT_EQ(reqs[0].last_attempt.value, 200);

  storage_->RemoveSyncRequest(topic, peer2);
}

// Test suite for history management
TEST_F(SQLiteStorageTest, History) {
  // Ensure contact exists for FK constraint
  identity::Contact const contact{peer1, identity::ContactStatus::confirmed};
  storage_->UpdateContact(contact);

  // Add a text message
  messaging::HistoryEntryRecord const text_entry{
      .kind = messaging::HistoryEntryKind::textMessage,
      .timestamp = Time::Now(),
      .direction = messaging::MessageDirection::sent,
      .status = messaging::MessageStatus::sending,
      .data = messaging::MessageEntry{"Hello"},
      .peer_id = peer1
  };
  auto added_text_entry = AddTestEntry(text_entry);
  EXPECT_NE(added_text_entry.id, 0);  // ID should be assigned by DB

  // Add a call record
  messaging::HistoryEntryRecord const call_entry{
      .kind = messaging::HistoryEntryKind::voiceCall,
      .timestamp = Time::Now(),
      .direction = messaging::MessageDirection::received,
      .data = messaging::CallEntry{30, messaging::CallDirection::incoming, messaging::CallStatus::answered},
      .peer_id = peer1
  };
  auto added_call_entry = AddTestEntry(call_entry);
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
  auto text_entry2 = AddTestEntry(
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
  identity::Contact const contact{peer1, identity::ContactStatus::confirmed};
  storage_->UpdateContact(contact);

  constexpr int total_messages = 25;
  uint64_t newest_id = 0;

  for (int i = 0; i < total_messages; ++i) {
    auto entry = AddTestEntry(
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

  auto const peer3_no_history = ContactID("fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025");

  auto const base_time = Time::Now();

  // History for peer1
  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 100,
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::none,
       .data = messaging::MessageEntry{"p1m1"},
       .peer_id = peer1}
  );
  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 200,
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::none,
       .data = messaging::MessageEntry{"p1m2"},
       .peer_id = peer1}
  );
  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 300,
       .direction = messaging::MessageDirection::sent,
       .status = messaging::MessageStatus::delivered,
       .data = messaging::MessageEntry{"p1m3"},
       .peer_id = peer1}
  );

  // History for peer2
  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = base_time + 50,
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::read,
       .data = messaging::MessageEntry{"p2m1"},
       .peer_id = peer2}
  );
  auto p2_last = AddTestEntry(
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

  auto entry = AddTestEntry(
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

  std::string const long_message(3000, 'a');

  auto entry = AddTestEntry(
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

  auto full_content = storage_->GetFullMessageContent(peer1, entry.id);
  ASSERT_TRUE(full_content.has_value());
  EXPECT_EQ(*full_content, long_message);
}

TEST_F(SQLiteStorageTest, MultiplePeersHistory) {
  // Ensure contacts exist
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  storage_->UpdateContact({peer2, identity::ContactStatus::confirmed});

  // Insert message for peer1
  auto entry1 = AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"P1"},
       .peer_id = peer1}
  );

  // Insert message for peer2
  auto entry2 = AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now() + 1,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"P2"},
       .peer_id = peer2}
  );

  // Insert message 2 for peer1
  auto entry3 = AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now() + 2,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"P1-2"},
       .peer_id = peer1}
  );

  // Verify peer1 history is independent and correctly ordered
  auto page1 = storage_->GetHistoryPage(peer1, 0, 10);
  ASSERT_EQ(page1.messages.size(), 2);
  EXPECT_EQ(page1.messages[0].id, entry1.id);
  EXPECT_EQ(page1.messages[1].id, entry3.id);

  // Verify peer2 history is independent
  auto page2 = storage_->GetHistoryPage(peer2, 0, 10);
  ASSERT_EQ(page2.messages.size(), 1);
  EXPECT_EQ(page2.messages[0].id, entry2.id);
}

TEST_F(SQLiteStorageTest, AttachmentPersistence) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});

  messaging::MessageEntry msg_data;
  msg_data.content = "Check this file";
  msg_data.attachments.push_back(
      {.id = "att-uuid-1",
       .type = messaging::AttachmentType::file,
       .file_path = "/tmp/doc.pdf",
       .mime_type = "application/pdf",
       .size_bytes = 1024 * 1024,
       .thumbnail_base64 = "thumbdata",
       .hash = "sha256hashstring",
       .is_downloaded = true}
  );

  auto entry = AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .data = msg_data,
       .peer_id = peer1}
  );

  // Retrieve
  auto page = storage_->GetHistoryPage(peer1, 0, 1);
  ASSERT_EQ(page.messages.size(), 1);

  ASSERT_TRUE(std::holds_alternative<messaging::MessageEntry>(page.messages[0].data));
  auto const& retrieved_msg = std::get<messaging::MessageEntry>(page.messages[0].data);

  ASSERT_EQ(retrieved_msg.attachments.size(), 1);
  auto const& att = retrieved_msg.attachments[0];

  EXPECT_EQ(att.id, "att-uuid-1");
  EXPECT_EQ(att.type, messaging::AttachmentType::file);
  EXPECT_EQ(att.file_path, "/tmp/doc.pdf");
  EXPECT_EQ(att.mime_type, "application/pdf");
  EXPECT_EQ(att.size_bytes, 1024 * 1024);
  EXPECT_EQ(att.thumbnail_base64, "thumbdata");
  EXPECT_EQ(att.hash, "sha256hashstring");
  EXPECT_TRUE(att.is_downloaded);
}

// --- Search Tests ---

TEST_F(SQLiteStorageTest, SearchSinglePeer) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"apple banana"},
       .peer_id = peer1}
  );

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 200,
       .direction = messaging::MessageDirection::received,
       .data = messaging::MessageEntry{"cherry date"},
       .peer_id = peer1}
  );

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 300,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"banana elderberry"},
       .peer_id = peer1}
  );

  auto results = storage_->SearchMessages(peer1, "banana");
  ASSERT_EQ(results.size(), 2);
  // Expect sorted by timestamp DESC
  EXPECT_EQ(std::get<messaging::MessageEntry>(results[0].data).content, "banana elderberry");
  EXPECT_EQ(std::get<messaging::MessageEntry>(results[1].data).content, "apple banana");

  auto results2 = storage_->SearchMessages(peer1, "Cherry");
  ASSERT_EQ(results2.size(), 1);
  EXPECT_EQ(std::get<messaging::MessageEntry>(results2[0].data).content, "cherry date");

  auto results3 = storage_->SearchMessages(peer1, "missing");
  EXPECT_TRUE(results3.empty());
}

TEST_F(SQLiteStorageTest, SearchLargeMessage) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});

  std::string const long_msg = std::string(3000, 'x') + "secret" + std::string(100, 'y');
  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{long_msg},
       .peer_id = peer1}
  );

  // Verify it was truncated
  auto page = storage_->GetHistoryPage(peer1, 0, 1);
  ASSERT_EQ(page.messages.size(), 1);
  EXPECT_TRUE(page.messages[0].is_truncated);

  // Search for term in the truncated part (assuming 'x' is at start)
  auto res1 = storage_->SearchMessages(peer1, "xxx");
  ASSERT_EQ(res1.size(), 1);

  // Search for term in the extended part
  auto res2 = storage_->SearchMessages(peer1, "secret");
  ASSERT_EQ(res2.size(), 1);
  // Verify full content is returned in search result for context
  auto const& msg_data = std::get<messaging::MessageEntry>(res2[0].data);
  if (res2[0].is_truncated && msg_data.content.size() < long_msg.size()) {
    auto full = storage_->GetFullMessageContent(peer1, res2[0].id);
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(*full, long_msg);
  } else {
    EXPECT_EQ(msg_data.content, long_msg);
  }
}

TEST_F(SQLiteStorageTest, SearchAllPeers) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  storage_->UpdateContact({peer2, identity::ContactStatus::confirmed});

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"common term p1"},
       .peer_id = peer1}
  );

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 200,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"common term p2"},
       .peer_id = peer2}
  );

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 300,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"unique p1"},
       .peer_id = peer1}
  );

  auto results = storage_->SearchAllMessages("common");
  ASSERT_EQ(results.size(), 2);
  // Sorted DESC
  EXPECT_EQ(results[0].peer_id, peer2);
  EXPECT_EQ(results[1].peer_id, peer1);

  auto res_p1 = storage_->SearchAllMessages("unique");
  ASSERT_EQ(res_p1.size(), 1);
  EXPECT_EQ(res_p1[0].peer_id, peer1);
}

TEST_F(SQLiteStorageTest, SearchFiltersNonText) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"hello"},
       .peer_id = peer1}
  );

  // Call entry - content is JSON in DB
  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::voiceCall,
       .timestamp = 200,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::CallEntry{},
       .peer_id = peer1}
  );

  // Searching for "hello" should find text
  auto res1 = storage_->SearchMessages(peer1, "hello");
  ASSERT_EQ(res1.size(), 1);

  // Searching for JSON keys (e.g. "duration") should NOT find the call,
  // as query restricts to text/system messages.
  auto res2 = storage_->SearchMessages(peer1, "duration");
  EXPECT_TRUE(res2.empty());
}

TEST_F(SQLiteStorageTest, SearchCaseInsensitive) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});

  AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = 100,
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"MixedCase Message"},
       .peer_id = peer1}
  );

  // Lowercase query finding mixed case content
  auto res1 = storage_->SearchMessages(peer1, "mixedcase");
  ASSERT_EQ(res1.size(), 1);
  EXPECT_EQ(std::get<messaging::MessageEntry>(res1[0].data).content, "MixedCase Message");

  // Uppercase query finding mixed case content
  auto res2 = storage_->SearchMessages(peer1, "MESSAGE");
  ASSERT_EQ(res2.size(), 1);

  // Mixed case query finding mixed case content
  auto res3 = storage_->SearchMessages(peer1, "mIxEdCaSe");
  ASSERT_EQ(res3.size(), 1);
}

TEST_F(SQLiteStorageTest, EditMessage) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  auto entry = AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"Original"},
       .peer_id = peer1}
  );

  storage_->UpdateMessageContent(peer1, entry.id, "Edited Content");

  auto loaded = storage_->GetHistoryEntry(peer1, entry.id);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(std::get<messaging::MessageEntry>(loaded->data).content, "Edited Content");
  EXPECT_TRUE(loaded->is_edited);
}

TEST_F(SQLiteStorageTest, DeleteMessage) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  auto entry = AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"Content"},
       .peer_id = peer1}
  );

  storage_->MarkMessageDeleted(peer1, entry.id);

  auto loaded = storage_->GetHistoryEntry(peer1, entry.id);
  ASSERT_TRUE(loaded);
  EXPECT_TRUE(loaded->is_deleted);
}

TEST_F(SQLiteStorageTest, Reactions) {
  storage_->UpdateContact({peer1, identity::ContactStatus::confirmed});
  auto entry = AddTestEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::sent,
       .data = messaging::MessageEntry{"Content"},
       .peer_id = peer1}
  );

  ContactID author = peer1;
  storage_->ToggleReaction(peer1, entry.id, author, messaging::Reaction::check);

  auto loaded = storage_->GetHistoryEntry(peer1, entry.id);
  ASSERT_TRUE(loaded);
  ASSERT_EQ(loaded->reactions.size(), 1);
  EXPECT_EQ(loaded->reactions[0].reaction, messaging::Reaction::check);

  // Toggle same reaction -> remove
  storage_->ToggleReaction(peer1, entry.id, author, messaging::Reaction::check);
  loaded = storage_->GetHistoryEntry(peer1, entry.id);
  EXPECT_TRUE(loaded->reactions.empty());
}
