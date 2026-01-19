#include "conversation_view_model.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>

#include "eillance/logging/logging.h"
#include "eillance/utils/time.hpp"

namespace eillance::view_model {

// --- Implementation Struct ---

struct ConversationViewModel::Impl : public std::enable_shared_from_this<Impl> {
  const ContactID peerId_;
  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  State state_;
  std::size_t chatPagesLoaded_ = 0;

  // A generation counter to invalidate results from stale load threads (e.g. after debug reset)
  std::atomic<int> loadGeneration_{0};

  static inline std::atomic<std::uint64_t> nextSystemMsgId_{10000000000000000000ULL};
  static constexpr size_t kPageSize = 50;

  Impl(ContactID peer_id, std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : peerId_(std::move(peer_id)), client_(std::move(client)), router_(std::move(router)) {
    state_.peer_id = peerId_;
  }

  void SetObserver(std::shared_ptr<Observer> observer) {
    const std::lock_guard<std::mutex> lock(mutex_);
    observer_ = std::move(observer);
    if (observer_) {
      observer_->OnHistoryUpdated(state_);
    }
  }

  auto GetState() const -> State {
    const std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  void SendText(const std::string& content) {
    auto entry = client_->SendChatMessage(peerId_, content);
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      state_.history.push_back(entry);
      if (observer_) { observer_->OnHistoryUpdated(state_); }
    }
  }

  void LoadOlderMessages() {
    const std::lock_guard<std::mutex> lock(mutex_);
    auto page = client_->GetMessageHistoryPage(peerId_, chatPagesLoaded_, kPageSize);

    if (!page.messages.empty()) {
      for (auto it = page.messages.rbegin(); it != page.messages.rend(); ++it) {
        state_.history.push_front(*it);
      }
      chatPagesLoaded_++;
      state_.has_older_messages = (chatPagesLoaded_ * kPageSize) < page.total_messages;
      if (observer_) { observer_->OnHistoryUpdated(state_); }
    } else if (state_.has_older_messages) {
      state_.has_older_messages = false;
      if (observer_) { observer_->OnHistoryUpdated(state_); }
    }
  }

  void LoadFullMessage(uint64_t message_id) {
    auto full_content = client_->GetFullMessageContent(message_id);
    if (!full_content) {
      LGW << "Could not load full content for message ID: " << message_id;
      return;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    bool updated = false;
    for (auto& entry: state_.history) {
      if (entry.id == message_id) {
        if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
          auto& msg_entry = std::get<messaging::MessageEntry>(entry.data);
          msg_entry.content = *full_content;
          entry.is_truncated = false;
          updated = true;
          break;
        }
      }
    }
    if (updated && observer_) {
      observer_->OnHistoryUpdated(state_);
    }
  }

  void MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids) {
    if (!message_ids.empty()) {
      client_->MarkMessagesAsRead(peerId_, message_ids);
    }
  }

  void AddSystemMessage(const std::string& message) {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.history.push_back({.id = nextSystemMsgId_++,
                              .kind = messaging::HistoryEntryKind::systemMessage,
                              .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),
                              .direction = messaging::MessageDirection::received,
                              .is_truncated = false,
                              .data = messaging::MessageEntry{message}});
    if (observer_) { observer_->OnHistoryUpdated(state_); }
  }

  void OnMessageReceived(const ContactID& /*peer_id*/, const messaging::HistoryEntryRecord& message) {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.history.push_back(message);
    if (observer_) { observer_->OnHistoryUpdated(state_); }
  }

  void OnMessageStatusChanged(const ContactID& /*peer_id*/, uint64_t message_id, messaging::MessageStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    bool changed = false;
    for (auto& entry: state_.history) {
      if (entry.id == message_id) {
        if (entry.status != status) {
          entry.status = status;
          changed = true;
        }
        break;
      }
    }
    if (changed && observer_) {
      observer_->OnHistoryUpdated(state_);
    }
  }

  // Runs on a background thread. 'self' keeps Impl alive during execution.
  void LoadInitialHistory(std::shared_ptr<Impl> self, int generation) {
    // Perform blocking DB op without holding the mutex
    auto page = client_->GetMessageHistoryPage(peerId_, 0, kPageSize);

    const std::lock_guard<std::mutex> lock(mutex_);
    // If generation changed (e.g. via reset), discard this result
    if (generation != loadGeneration_) {
      return;
    }

    state_.history.assign(page.messages.begin(), page.messages.end());
    chatPagesLoaded_ = 1;
    state_.has_older_messages = (kPageSize < page.total_messages);
    if (observer_) { observer_->OnHistoryUpdated(state_); }
  }

  void StartInitialLoad() {
    int gen = ++loadGeneration_;
    // Detach the thread so it doesn't block destruction of the wrapper.
    // Pass shared_from_this() to keep Impl alive until thread finishes.
    std::thread([this, self = shared_from_this(), gen] {
      LoadInitialHistory(self, gen);
    }).detach();
  }
};

// --- ConversationViewModel Wrapper ---

ConversationViewModel::ConversationViewModel(const ContactID& peerId, std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : impl_(std::make_shared<Impl>(peerId, std::move(client), std::move(router))) {

  impl_->StartInitialLoad();

  auto& queue = impl_->router_->Queue();
  Subscribe<EventChatMessageReceived>(queue, [this](const EventChatMessageReceived& event) {
    if (event.peer_id == impl_->peerId_) {
      impl_->OnMessageReceived(event.peer_id, event.message);
    }
  });
  Subscribe<EventMessageStatusChanged>(queue, [this](const EventMessageStatusChanged& event) {
    if (event.peer_id == impl_->peerId_) {
      impl_->OnMessageStatusChanged(event.peer_id, event.message_id, event.status);
    }
  });
  Subscribe<EventDebugDataReset>(queue, [this](const auto&) {
    impl_->StartInitialLoad();
  });
}

ConversationViewModel::~ConversationViewModel() {
  for (const auto& unsubscribe: eventUnsubscribers_) {
    unsubscribe();
  }
  // No join() needed; Impl is kept alive by the detached thread if it's still running.
}

template<typename EventType>
void ConversationViewModel::Subscribe(EventQueue& queue, std::function<void(const EventType&)> callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void ConversationViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  impl_->SetObserver(std::move(observer));
}

auto ConversationViewModel::GetState() const -> State {
  return impl_->GetState();
}

void ConversationViewModel::SendText(const std::string& content) {
  impl_->SendText(content);
}

void ConversationViewModel::LoadOlderMessages() {
  impl_->LoadOlderMessages();
}

void ConversationViewModel::LoadFullMessage(uint64_t messageId) {
  impl_->LoadFullMessage(messageId);
}

void ConversationViewModel::MarkMessagesAsRead(const std::vector<std::uint64_t>& messageIds) {
  impl_->MarkMessagesAsRead(messageIds);
}

void ConversationViewModel::AddSystemMessage(const std::string& message) {
  impl_->AddSystemMessage(message);
}

}  // namespace eillance::view_model
