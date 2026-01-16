#include "conversation_view_model.hpp"

#include <chrono>
#include <variant>

#include "eillance/logging/logging.h"
#include "eillance/utils/time.hpp"

namespace eillance::view_model {

ConversationViewModel::ConversationViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier)
    : client_(std::move(client)), notifier_(std::move(notifier)) {
}

auto ConversationViewModel::GetState() const -> State {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

auto ConversationViewModel::GetSelectedContactId() const -> std::optional<ContactID> {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_.selected_contact_id;
}

void ConversationViewModel::SelectContact(const ContactID& peer_id) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.selected_contact_id = peer_id;
    LoadInitialHistory(peer_id);
  }
  if (notifier_) {
    notifier_(StateChange::SelectedContact);
    notifier_(StateChange::InteractionHistory);
  }
}

void ConversationViewModel::DeselectContact() {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.selected_contact_id.has_value()) {
      state_.selected_contact_id.reset();
      state_.history.clear();
      state_.has_older_messages = false;
      changed = true;
    }
  }
  if (changed && notifier_) {
    notifier_(StateChange::SelectedContact);
    notifier_(StateChange::InteractionHistory);
  }
}

void ConversationViewModel::SendMessage(const std::string& content) {
  std::optional<ContactID> selected_id;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    selected_id = state_.selected_contact_id;
  }

  if (!selected_id) {
    LOGW << "SendMessage called with no selected contact.";
    return;
  }

  auto entry = client_->SendChatMessage(*selected_id, content);

  {
    const std::lock_guard<std::mutex> lock(mutex_);
    // Verify selection hasn't changed
    if (state_.selected_contact_id == selected_id) {
      state_.history.push_back(entry);
    }
  }

  if (notifier_) notifier_(StateChange::InteractionHistory);
}

void ConversationViewModel::LoadOlderMessages() {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.selected_contact_id) return;

    const auto& peer_id = *state_.selected_contact_id;
    const size_t pages_loaded = chat_pages_loaded_[peer_id];

    auto page = client_->GetMessageHistoryPage(peer_id, pages_loaded, kPageSize);
    if (!page.messages.empty()) {
      for (auto it = page.messages.rbegin(); it != page.messages.rend(); ++it) {
        state_.history.push_front(*it);
      }
      chat_pages_loaded_[peer_id]++;
      state_.has_older_messages = (chat_pages_loaded_[peer_id] * kPageSize) < page.total_messages;
      changed = true;
    } else if (state_.has_older_messages) {
      state_.has_older_messages = false;
      changed = true;
    }
  }
  if (changed && notifier_) notifier_(StateChange::InteractionHistory);
}

void ConversationViewModel::LoadFullMessage(uint64_t message_id) {
  auto full_content = client_->GetFullMessageContent(message_id);
  if (!full_content) {
    LOGW << "Could not load full content for message ID: " << message_id;
    return;
  }

  bool updated = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.selected_contact_id) {
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
    }
  }
  if (updated && notifier_) notifier_(StateChange::InteractionHistory);
}

void ConversationViewModel::MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids) {
  std::optional<ContactID> peer_id;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    peer_id = state_.selected_contact_id;
  }
  if (peer_id && !message_ids.empty()) {
    client_->MarkMessagesAsRead(*peer_id, message_ids);
    // No direct notification needed as we usually update UI immediately via local state if we want,
    // but the actual confirmation comes via callback.
    // However, usually we want to reflect "Read" status locally immediately if it's our own action.
    // Client::MarkMessagesAsRead updates DB.
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
  if (notifier_) notifier_(StateChange::InteractionHistory);
}

void ConversationViewModel::OnMessageReceived(const ContactID& peer_id, const messaging::HistoryEntryRecord& message) {
  bool updated = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.selected_contact_id && *state_.selected_contact_id == peer_id) {
      state_.history.push_back(message);
      updated = true;
    }
  }
  if (updated && notifier_) notifier_(StateChange::InteractionHistory);
}

void ConversationViewModel::OnMessageStatusChanged(const ContactID& peer_id, uint64_t message_id, messaging::MessageStatus status) {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.selected_contact_id && *state_.selected_contact_id == peer_id) {
      for (auto& entry: state_.history) {
        if (entry.id == message_id) {
          if (entry.status != status) {
            entry.status = status;
            changed = true;
          }
          break;
        }
      }
    }
  }
  if (changed && notifier_) notifier_(StateChange::InteractionHistory);
}

void ConversationViewModel::DebugEraseAllHistory() {
  client_->DebugEraseAllHistory();
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.history.clear();
    state_.has_older_messages = false;
  }
  if (notifier_) notifier_(StateChange::InteractionHistory);
}

void ConversationViewModel::LoadInitialHistory(const ContactID& peer_id) {
  // Assumes mutex is locked by caller
  auto page = client_->GetMessageHistoryPage(peer_id, 0, kPageSize);
  state_.history.assign(page.messages.begin(), page.messages.end());
  chat_pages_loaded_[peer_id] = 1;
  state_.has_older_messages = (kPageSize < page.total_messages);
}

}  // namespace eillance::view_model
