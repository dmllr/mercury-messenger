#include "conversation_view_model.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>

#include <ranges>

#include "eillance/logging/logging.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::view_model {

// --- Implementation Struct ---

struct ConversationViewModel::Impl : public std::enable_shared_from_this<Impl> {
  ContactID const peer_id;
  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  State state_;
  std::size_t chatPagesLoaded_ = 0;

  // A generation counter to invalidate results from stale load threads (e.g. after debug reset)
  std::atomic<int> loadGeneration_{0};

  inline static std::atomic<std::uint64_t> nextSystemMsgId{10000000000000000000ULL};
  static constexpr size_t kPageSize = 50;

  Impl(ContactID peer_id, std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : peer_id(peer_id),
        client_(std::move(client)),
        router_(std::move(router)) {
    state_.peer_id = peer_id;
  }

  void SetObserver(std::shared_ptr<Observer> observer) {
    std::lock_guard<std::mutex> const lock(mutex_);
    observer_ = std::move(observer);
    if (observer_) {
      observer_->OnHistoryUpdated(state_);
    }
  }

  auto GetState() const -> State {
    std::lock_guard<std::mutex> const lock(mutex_);
    return state_;
  }

  void SendText(std::string const& content) {
    auto entry = client_->SendChatMessage(peer_id, content);
    {
      std::lock_guard<std::mutex> const lock(mutex_);
      state_.history.push_back(entry);
      if (observer_) {
        observer_->OnHistoryUpdated(state_);
      }
    }
  }

  void LoadOlderMessages() {
    std::lock_guard<std::mutex> const lock(mutex_);
    auto page = client_->GetMessageHistoryPage(peer_id, chatPagesLoaded_, kPageSize);

    if (!page.messages.empty()) {
      for (auto& message : std::ranges::reverse_view(page.messages)) {
        state_.history.push_front(message);
      }
      chatPagesLoaded_++;
      state_.has_older_messages = (chatPagesLoaded_ * kPageSize) < page.total_messages;
      if (observer_) {
        observer_->OnHistoryUpdated(state_);
      }
    } else if (state_.has_older_messages) {
      state_.has_older_messages = false;
      if (observer_) {
        observer_->OnHistoryUpdated(state_);
      }
    }
  }

  void LoadFullMessage(uint64_t message_id) {
    auto full_content = client_->GetFullMessageContent(message_id);
    if (!full_content) {
      LGW << "Could not load full content for message ID: " << message_id;
      return;
    }

    std::lock_guard<std::mutex> const lock(mutex_);
    bool updated = false;
    for (auto& entry : state_.history) {
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

  void MarkMessagesAsRead(std::vector<std::uint64_t> const& message_ids) {
    if (!message_ids.empty()) {
      client_->MarkMessagesAsRead(peer_id, message_ids);
    }
  }

  void AddSystemMessage(std::string const& message) {
    std::lock_guard<std::mutex> const lock(mutex_);
    state_.history.push_back(
        {.id = nextSystemMsgId++,
         .kind = messaging::HistoryEntryKind::systemMessage,
         .timestamp = static_cast<Time::Unit>(
             std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                 .count()
         ),
         .direction = messaging::MessageDirection::received,
         .is_truncated = false,
         .data = messaging::MessageEntry{message}}
    );
    if (observer_) {
      observer_->OnHistoryUpdated(state_);
    }
  }

  void OnMessageReceived(ContactID const& /*peer_id*/, messaging::HistoryEntryRecord const& message) {
    std::lock_guard<std::mutex> const lock(mutex_);
    state_.history.push_back(message);
    if (observer_) {
      observer_->OnHistoryUpdated(state_);
    }
  }

  void OnMessageStatusChanged(ContactID const& /*peer_id*/, uint64_t message_id, messaging::MessageStatus status) {
    std::lock_guard<std::mutex> const lock(mutex_);
    bool changed = false;
    for (auto& entry : state_.history) {
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
  void LoadInitialHistory(std::shared_ptr<Impl> const& /*self*/, int generation) {
    // Perform blocking DB op without holding the mutex
    auto page = client_->GetMessageHistoryPage(peer_id, 0, kPageSize);

    std::lock_guard<std::mutex> const lock(mutex_);
    // If generation changed (e.g. via reset), discard this result
    if (generation != loadGeneration_) {
      return;
    }

    state_.history.assign(page.messages.begin(), page.messages.end());
    chatPagesLoaded_ = 1;
    state_.has_older_messages = (kPageSize < page.total_messages);
    if (observer_) {
      observer_->OnHistoryUpdated(state_);
    }
  }

  void StartInitialLoad() {
    int const gen = ++loadGeneration_;
    // Detach the thread so it doesn't block destruction of the wrapper.
    // Pass shared_from_this() to keep Impl alive until thread finishes.
    std::thread([this, self = shared_from_this(), gen] { LoadInitialHistory(self, gen); }).detach();
  }
};

// --- ConversationViewModel Wrapper ---

ConversationViewModel::ConversationViewModel(
    ContactID const& peerId,
    std::shared_ptr<Client> client,
    std::shared_ptr<ClientEventRouter> router
)
    : impl_(std::make_shared<Impl>(peerId, std::move(client), std::move(router))) {
  impl_->StartInitialLoad();

  auto& queue = impl_->router_->Queue();
  Subscribe<EventChatMessageReceived>(queue, [this](EventChatMessageReceived const& event) {
    if (event.peer_id == impl_->peer_id) {
      impl_->OnMessageReceived(event.peer_id, event.message);
    }
  });
  Subscribe<EventMessageStatusChanged>(queue, [this](EventMessageStatusChanged const& event) {
    if (event.peer_id == impl_->peer_id) {
      impl_->OnMessageStatusChanged(event.peer_id, event.message_id, event.status);
    }
  });
  Subscribe<EventDebugDataReset>(queue, [this](auto const&) { impl_->StartInitialLoad(); });
}

ConversationViewModel::~ConversationViewModel() {
  for (auto const& unsubscribe : eventUnsubscribers_) {
    unsubscribe();
  }
  // No join() needed; Impl is kept alive by the detached thread if it's still running.
}

template <typename EventType>
void ConversationViewModel::Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void ConversationViewModel::SetObserver(std::shared_ptr<Observer> observer) const {
  impl_->SetObserver(std::move(observer));
}

auto ConversationViewModel::GetState() const -> State { return impl_->GetState(); }

void ConversationViewModel::SendText(std::string const& content) const { impl_->SendText(content); }

void ConversationViewModel::LoadOlderMessages() const { impl_->LoadOlderMessages(); }

void ConversationViewModel::LoadFullMessage(uint64_t const messageId) const { impl_->LoadFullMessage(messageId); }

void ConversationViewModel::MarkMessagesAsRead(std::vector<std::uint64_t> const& messageIds) const {
  impl_->MarkMessagesAsRead(messageIds);
}

void ConversationViewModel::AddSystemMessage(std::string const& message) const { impl_->AddSystemMessage(message); }

}  // namespace eillance::view_model
