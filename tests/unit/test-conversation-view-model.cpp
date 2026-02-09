#include <cstring>
#include <map>
#include <mutex>
#include <variant>

#include "eillance/view_model/conversation_view_model.hpp"
#include "test-view-models-common.hpp"

using namespace eillance::view_model;

class ConversationViewModelTest : public ViewModelTest {
 protected:
  void SetUp() override {
    ViewModelTest::SetUp();
    // Pre-add contact so we can chat. Wait for it to propagate to storage.
    client_->AddContact(peer1, std::nullopt);

    // Give time for AddContact thread to write to storage and generate system message
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    vm_ = std::make_shared<ConversationViewModel>(peer1, client_, router_);

    // Give time for InitialLoad thread to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::shared_ptr<ConversationViewModel> vm_;
};

struct MockConversationObserver : public ConversationViewModel::Observer {
  ConversationViewModel::State state;
  bool updated = false;
  std::mutex mutex;
  std::map<std::string, float> progress_updates;

  void OnHistoryUpdated(ConversationViewModel::State const& s) override {
    std::lock_guard<std::mutex> const lock(mutex);
    state = s;
    updated = true;
  }

  void OnTransferProgress(std::string const& attachment_id, float progress) override {
    std::lock_guard<std::mutex> const lock(mutex);
    progress_updates[attachment_id] = progress;
  }

  auto GetState() -> ConversationViewModel::State {
    std::lock_guard<std::mutex> const lock(mutex);
    return state;
  }

  auto GetProgress(std::string const& id) -> float {
    std::lock_guard<std::mutex> const lock(mutex);
    if (progress_updates.count(id)) return progress_updates[id];
    return -1.0f;
  }
};

TEST_F(ConversationViewModelTest, InitialState) {
  auto observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);

  // Even if empty, SetObserver triggers an update.
  WaitFor([&] { return observer->updated; });

  auto state = observer->GetState();
  EXPECT_EQ(state.peer_id, peer1);
}

TEST_F(ConversationViewModelTest, SendMessage) {
  auto observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);
  WaitFor([&] { return observer->updated; });

  {
    std::lock_guard<std::mutex> const lock(observer->mutex);
    observer->updated = false;
  }

  vm_->SendText("Hello World");

  // Find the specific message, ignoring system messages
  WaitFor([&] {
    auto s = observer->GetState();
    for (auto const& msg : s.history) {
      if (std::holds_alternative<messaging::MessageEntry>(msg.data)) {
        if (std::get<messaging::MessageEntry>(msg.data).content == "Hello World") return true;
      }
    }
    return false;
  });

  auto state = observer->GetState();
  bool found = false;
  for (auto const& msg : state.history) {
    if (std::holds_alternative<messaging::MessageEntry>(msg.data)) {
      auto const& data = std::get<messaging::MessageEntry>(msg.data);
      if (data.content == "Hello World") {
        EXPECT_EQ(msg.direction, messaging::MessageDirection::sent);
        // Expect Sending, never Sent
        EXPECT_EQ(msg.status, messaging::MessageStatus::sending);
        found = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(ConversationViewModelTest, ReceiveMessage) {
  auto observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);
  WaitFor([&] { return observer->updated; });

  // Simulate receiving a message via the router/client logic.
  auto entry = mock_storage_->AddHistoryEntry(
      {.kind = messaging::HistoryEntryKind::textMessage,
       .timestamp = Time::Now(),
       .direction = messaging::MessageDirection::received,
       .status = messaging::MessageStatus::read,
       .data = messaging::MessageEntry{"Hi there"},
       .peer_id = peer1}
  );

  // Push event to router queue (simulating what Client would do)
  router_->Queue().Push(EventChatMessageReceived{peer1, entry});

  WaitFor([&] {
    auto s = observer->GetState();
    if (s.history.empty()) return false;
    // Search for our message
    for (auto const& msg : s.history) {
      if (std::holds_alternative<messaging::MessageEntry>(msg.data)) {
        if (std::get<messaging::MessageEntry>(msg.data).content == "Hi there") return true;
      }
    }
    return false;
  });

  auto state = observer->GetState();
  bool found = false;
  for (auto const& msg : state.history) {
    if (std::holds_alternative<messaging::MessageEntry>(msg.data)) {
      if (std::get<messaging::MessageEntry>(msg.data).content == "Hi there") {
        EXPECT_EQ(msg.direction, messaging::MessageDirection::received);
        found = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(ConversationViewModelTest, MessageStatusUpdate) {
  vm_->SendText("Status Test");
  auto observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);

  // Wait until the message appears in history
  uint64_t msg_id = 0;
  WaitFor([&] {
    auto s = observer->GetState();
    for (auto const& msg : s.history) {
      if (std::holds_alternative<messaging::MessageEntry>(msg.data) &&
          std::get<messaging::MessageEntry>(msg.data).content == "Status Test") {
        msg_id = msg.id;
        return true;
      }
    }
    return false;
  });

  // Simulate status update event
  router_->Queue().Push(EventMessageStatusChanged{peer1, msg_id, messaging::MessageStatus::delivered});

  WaitFor([&] {
    auto s = observer->GetState();
    for (auto const& msg : s.history) {
      if (msg.id == msg_id && msg.status == messaging::MessageStatus::delivered) {
        return true;
      }
    }
    return false;
  });

  auto state = observer->GetState();
  bool found = false;
  for (auto const& msg : state.history) {
    if (msg.id == msg_id) {
      EXPECT_EQ(msg.status, messaging::MessageStatus::delivered);
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST_F(ConversationViewModelTest, TransferProgressUpdates) {
  auto observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);

  std::string att_id = "test-att-id";

  // Simulate progress events
  router_->Queue().Push(EventTransferProgress{peer1, att_id, 0.1f});
  WaitFor([&] { return observer->GetProgress(att_id) >= 0.1f; });
  EXPECT_NEAR(observer->GetProgress(att_id), 0.1f, 0.001f);

  router_->Queue().Push(EventTransferProgress{peer1, att_id, 0.5f});
  WaitFor([&] { return observer->GetProgress(att_id) >= 0.5f; });
  EXPECT_NEAR(observer->GetProgress(att_id), 0.5f, 0.001f);

  router_->Queue().Push(EventTransferProgress{peer1, att_id, 1.0f});
  WaitFor([&] { return observer->GetProgress(att_id) >= 1.0f; });
  EXPECT_NEAR(observer->GetProgress(att_id), 1.0f, 0.001f);
}

TEST_F(ConversationViewModelTest, CancelTransferPropagates) {
  auto observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);

  // Setup message with attachment
  messaging::Attachment att;
  att.id = "cancel-test-id";
  att.status = messaging::AttachmentStatus::transferring;
  att.is_downloaded = false;

  messaging::MessageEntry msg_data;
  msg_data.attachments.push_back(att);

  auto entry = mock_storage_->AddHistoryEntry({.data = msg_data, .peer_id = peer1});

  // Inject into VM (simulate load or receive)
  router_->Queue().Push(EventChatMessageReceived{peer1, entry});
  WaitFor([&] { return !observer->GetState().history.empty(); });

  entry.direction = messaging::MessageDirection::received;  // Force download scenario
  mock_storage_->UpdateHistoryEntry(entry);                 // Update DB state just in case

  // Cancel transfer via VM.
  vm_->CancelTransfer("cancel-test-id", false);

  // Wait for status change
  WaitFor([&] {
    auto s = observer->GetState();
    for (auto const& msg : s.history) {
      if (std::holds_alternative<messaging::MessageEntry>(msg.data)) {
        auto& m = std::get<messaging::MessageEntry>(msg.data);
        for (auto& a : m.attachments) {
          // Download cancellation results in PAUSED state locally
          if (a.id == "cancel-test-id" && a.status == messaging::AttachmentStatus::paused) return true;
        }
      }
    }
    return false;
  });

  // Check transport log to confirm PAUSE message sent (Binary 0x04)
  bool pause_sent = false;
  for (auto const& msg : mock_transport_->log.sent_binary_messages | std::views::values) {
    if (msg.size() >= 3 && msg[0] == std::byte{0xFE} && msg[1] == std::byte{0x04}) {  // 0x04 = PAUSE
      if (auto const id_len = static_cast<uint8_t>(msg[2]); msg.size() >= 3 + id_len) {
        std::string id(id_len, '\0');
        std::memcpy(id.data(), msg.data() + 3, id_len);
        if (id == "cancel-test-id") {
          pause_sent = true;
        }
      }
    }
  }
  ASSERT_TRUE(pause_sent);
}

TEST_F(ConversationViewModelTest, ResumeTransferPropagates) {
  auto const observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);

  // If connection is not open, it might be queued.
  // Connect and simulate data channel open to ensure sending
  client_->Connect(peer1);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  // Simulate DC open
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Resume download (is_upload=false)
  vm_->ResumeTransfer("resume-test-id", false);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  bool req_sent = false;
  for (auto const& msg : mock_transport_->log.sent_binary_messages | std::views::values) {
    // Check if it's a binary Resume packet (Magic 0xFE, Type 0x05)
    if (msg.size() >= 3 && msg[0] == std::byte{0xFE} && msg[1] == std::byte{0x05}) {
      if (auto const id_len = static_cast<uint8_t>(msg[2]); msg.size() >= 3 + id_len) {
        std::string id(id_len, '\0');
        std::memcpy(id.data(), msg.data() + 3, id_len);
        if (id == "resume-test-id") {
          req_sent = true;
        }
      }
    }
  }
  EXPECT_TRUE(req_sent);
}

TEST_F(ConversationViewModelTest, SendSmallAttachment_UpdatesStatusSynchronously) {
  auto const observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);

  // Establish connection so upload starts immediately
  client_->Connect(peer1);
  // Yield to let connection setup (AddPeer etc.)
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Simulate Data Channel Open to enable sending
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto const peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);
  eillance::crypto::SessionCipher const other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  peer->data_channel_is_open = true;

  // Create small temp file
  auto const temp_path = std::filesystem::temp_directory_path() / "cvm_small.dat";
  {
    std::ofstream ofs(temp_path, std::ios::binary);
    ofs << "small data";
  }

  // Send
  std::vector<services::AttachmentRequest> atts;
  atts.push_back({temp_path.string(), messaging::AttachmentType::file, "", "application/octet-stream"});
  vm_->SendMessage("Small File", atts);

  // Wait for Done status
  bool success = false;
  WaitFor(
      [&] {
        auto state = observer->GetState();
        for (auto const& entry : state.history) {
          if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
            auto const& [content, attachments] = std::get<messaging::MessageEntry>(entry.data);
            if (content == "Small File" && !attachments.empty()) {
              if (attachments[0].status == messaging::AttachmentStatus::done) {
                success = true;
                return true;
              }
            }
          }
        }
        return false;
      },
      std::chrono::milliseconds(2000)
  );

  EXPECT_TRUE(success) << "Attachment status should be Done for small file sent synchronously";

  std::filesystem::remove(temp_path);
}

TEST_F(ConversationViewModelTest, LoadUpToMessage) {
  auto observer = std::make_shared<MockConversationObserver>();

  // Create 150 messages in mock storage
  uint64_t target_id = 0;
  for (int i = 0; i < 150; ++i) {
    auto entry = mock_storage_->AddHistoryEntry(
        {.kind = messaging::HistoryEntryKind::textMessage,
         .timestamp = Time::Now() + i,
         .direction = messaging::MessageDirection::sent,
         .status = messaging::MessageStatus::delivered,
         .data = messaging::MessageEntry{"Message " + std::to_string(i)},
         .peer_id = peer1}
    );
    if (i == 20) {  // Deep in history
      target_id = entry.id;
    }
  }

  // Re-initialize VM to simulate fresh entry
  auto vm = std::make_shared<ConversationViewModel>(peer1, client_, router_);
  vm->SetObserver(observer);
  WaitFor([&] { return observer->GetState().history.size() > 0; });

  // Initially, it should only load 50 messages
  auto initial_state = observer->GetState();
  EXPECT_EQ(initial_state.history.size(), 50);

  // Call LoadUpToMessage
  vm->LoadUpToMessage(target_id);

  // Wait for history to expand to include target_id
  WaitFor([&] {
    auto s = observer->GetState();
    for (auto const& msg : s.history) {
      if (msg.id == target_id) return true;
    }
    return false;
  });

  auto final_state = observer->GetState();
  bool found = false;
  for (auto const& msg : final_state.history) {
    if (msg.id == target_id) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
  EXPECT_GT(final_state.history.size(), 50);
}

TEST_F(ConversationViewModelTest, LoadUpToMessage_OverridesInitialLoad) {
  auto observer = std::make_shared<MockConversationObserver>();

  // Setup: Create messages
  uint64_t target_id = 0;
  for (int i = 0; i < 60; ++i) {
    auto entry = mock_storage_->AddHistoryEntry(
        {.kind = messaging::HistoryEntryKind::textMessage,
         .timestamp = Time::Now() + i,
         .direction = messaging::MessageDirection::sent,
         .status = messaging::MessageStatus::delivered,
         .data = messaging::MessageEntry{"msg"},
         .peer_id = peer1}
    );
    if (i == 5) target_id = entry.id;  // Older message, not in first page of 50
  }

  // Re-create VM. This triggers StartInitialLoad in a thread.
  auto vm = std::make_shared<ConversationViewModel>(peer1, client_, router_);
  vm->SetObserver(observer);

  // Immediately request loading specific message.
  // This should increment load_generation, causing the StartInitialLoad to abort
  // when/if it tries to apply results.
  vm->LoadUpToMessage(target_id);

  // Wait for update containing the target
  WaitFor([&] {
    auto s = observer->GetState();
    for (auto const& msg : s.history) {
      if (msg.id == target_id) return true;
    }
    return false;
  });

  // Verify we have the message
  auto final_state = observer->GetState();
  bool present = false;
  for (auto const& msg : final_state.history) {
    if (msg.id == target_id) present = true;
  }
  EXPECT_TRUE(present);
  // Ensure we have loaded enough history
  EXPECT_GT(final_state.history.size(), 50);
}
