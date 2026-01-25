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
