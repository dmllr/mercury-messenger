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
        // Accept either sending or sent, as it races with the delivery service
        EXPECT_TRUE(msg.status == messaging::MessageStatus::sending || msg.status == messaging::MessageStatus::sent);
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

  // Cancel transfer via VM
  vm_->CancelTransfer("cancel-test-id", false);

  // Wait for status change
  WaitFor([&] {
    auto s = observer->GetState();
    for (auto const& msg : s.history) {
      if (std::holds_alternative<messaging::MessageEntry>(msg.data)) {
        auto& m = std::get<messaging::MessageEntry>(msg.data);
        for (auto& a : m.attachments) {
          if (a.id == "cancel-test-id" && a.status == messaging::AttachmentStatus::canceled) return true;
        }
      }
    }
    return false;
  });

  // Check transport log to confirm cancel message sent (Binary 0x03)
  bool cancel_sent = false;
  for (auto const& msg : mock_transport_->log.sent_binary_messages | std::views::values) {
    if (msg.size() >= 3 && msg[0] == std::byte{0xFE} && msg[1] == std::byte{0x03}) {
      if (auto const id_len = static_cast<uint8_t>(msg[2]); msg.size() >= 3 + id_len) {
        std::string id(id_len, '\0');
        std::memcpy(id.data(), msg.data() + 3, id_len);
        if (id == "cancel-test-id") {
          cancel_sent = true;
        }
      }
    }
  }
  ASSERT_TRUE(cancel_sent);
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
  auto observer = std::make_shared<MockConversationObserver>();
  vm_->SetObserver(observer);

  // 1. Establish connection so upload starts immediately
  client_->Connect(peer1);
  // Yield to let connection setup (AddPeer etc)
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Simulate Data Channel Open to enable sending
  mock_transport_->delegate->OnDataChannelOpen(peer1);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto peer = mock_transport_->GetPeer(peer1);
  ASSERT_TRUE(peer);
  eillance::crypto::SessionCipher other;
  peer->session_cipher->ComputeSharedSecret(other.GetPublicKey());
  peer->data_channel_is_open = true;

  // 2. Create small temp file
  auto temp_path = std::filesystem::temp_directory_path() / "cvm_small.dat";
  {
    std::ofstream ofs(temp_path, std::ios::binary);
    ofs << "small data";
  }

  // 3. Send
  std::vector<services::AttachmentRequest> atts;
  atts.push_back({temp_path.string(), messaging::AttachmentType::file, "", "application/octet-stream"});
  vm_->SendMessage("Small File", atts);

  // 4. Wait for Done status
  bool success = false;
  WaitFor(
      [&] {
        auto s = observer->GetState();
        for (auto const& entry : s.history) {
          if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
            auto const& msg = std::get<messaging::MessageEntry>(entry.data);
            if (msg.content == "Small File" && !msg.attachments.empty()) {
              if (msg.attachments[0].status == messaging::AttachmentStatus::done) {
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
