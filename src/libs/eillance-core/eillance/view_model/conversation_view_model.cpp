#include "conversation_view_model.hpp"

#include <chrono>
#include <variant>

#include "eillance/logging/logging.h"
#include "eillance/utils/time.hpp"

namespace eillance::view_model {

ConversationViewModel::ConversationViewModel(const ContactID& peer_id, std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : peer_id_(peer_id), client_(std::move(client)), router_(std::move(router)) {
  state_.peer_id = peer_id_;
  LoadInitialHistory();

  auto& q = router_->Queue();
  Subscribe<EventChatMessageReceived>(q, [this](const EventChatMessageReceived& e) {
    if (e.peer_id == peer_id_) {
      OnMessageReceived(e.peer_id, e.message);
    }
  });
  Subscribe<EventMessageStatusChanged>(q, [this](const EventMessageStatusChanged& e) {
    if (e.peer_id == peer_id_) {
      OnMessageStatusChanged(e.peer_id, e.message_id, e.status);
    }
  });
  Subscribe<EventDebugDataReset>(q, [this](const auto&) { LoadInitialHistory(); });
}

ConversationViewModel::~ConversationViewModel() {
  for (const auto& unsubscribe: event_unsubscribers_) {
    unsubscribe();
  }
}

template<typename EventType>
void ConversationViewModel::Subscribe(EventQueue& q, std::function<void(const EventType&)> callback) {
  auto id = q.Subscribe<EventType>(std::move(callback));
  event_unsubscribers_.push_back([&q, id] { q.Unsubscribe<EventType>(id); });
}

void ConversationViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  const std::lock_guard<std::mutex> lock(mutex_);
  observer_ = std::move(observer);
  if (observer_) {
    observer_->OnHistoryUpdated(state_);
  }
}

auto ConversationViewModel::GetState() const -> State {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void ConversationViewModel::SendText(const std::string& content) {
  auto entry = client_->SendChatMessage(peer_id_, content);
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.history.push_back(entry);
    if (observer_) { observer_->OnHistoryUpdated(state_); }
  }
}

void ConversationViewModel::LoadOlderMessages() {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto page = client_->GetMessageHistoryPage(peer_id_, chat_pages_loaded_, kPageSize);

  if (!page.messages.empty()) {
    for (auto it = page.messages.rbegin(); it != page.messages.rend(); ++it) {
      state_.history.push_front(*it);
    }
    chat_pages_loaded_++;
    state_.has_older_messages = (chat_pages_loaded_ * kPageSize) < page.total_messages;
    if (observer_) { observer_->OnHistoryUpdated(state_); }
  } else if (state_.has_older_messages) {
    state_.has_older_messages = false;
    if (observer_) { observer_->OnHistoryUpdated(state_); }
  }
}

void ConversationViewModel::LoadFullMessage(uint64_t message_id) {
  auto full_content = client_->GetFullMessageContent(message_id);
  if (!full_content) {
    LOGW << "Could not load full content for message ID: " << message_id;
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

void ConversationViewModel::MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids) {
  if (!message_ids.empty()) {
    client_->MarkMessagesAsRead(peer_id_, message_ids);
  }
}

void ConversationViewModel::AddSystemMessage(const std::string& message) {
  const std::lock_guard<std::mutex> lock(mutex_);
  state_.history.push_back({.id = next_system_msg_id_++,
                            .kind = messaging::HistoryEntryKind::systemMessage,
                            .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()),
                            .direction = messaging::MessageDirection::received,
                            .is_truncated = false,
                            .data = messaging::MessageEntry{message}});
  if (observer_) { observer_->OnHistoryUpdated(state_); }
}

void ConversationViewModel::OnMessageReceived(const ContactID& /*peer_id*/, const messaging::HistoryEntryRecord& message) {
  const std::lock_guard<std::mutex> lock(mutex_);
  state_.history.push_back(message);
  if (observer_) { observer_->OnHistoryUpdated(state_); }
}

void ConversationViewModel::OnMessageStatusChanged(const ContactID& /*peer_id*/, uint64_t message_id, messaging::MessageStatus status) {
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

void ConversationViewModel::LoadInitialHistory() {
  const std::lock_guard<std::mutex> lock(mutex_);
  auto page = client_->GetMessageHistoryPage(peer_id_, 0, kPageSize);
  state_.history.assign(page.messages.begin(), page.messages.end());
  chat_pages_loaded_ = 1;
  state_.has_older_messages = (kPageSize < page.total_messages);
  if (observer_) { observer_->OnHistoryUpdated(state_); }
}

}  // namespace eillance::view_model
