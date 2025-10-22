#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/utils/dispatch_queue.hpp"
#include "eillance/rtc/client.hpp"

namespace eillance::view_model {

// UIState holds all the data required for any UI to render its current state.
// It is designed to be immutable once obtained by the UI to ensure thread safety.
struct UIState {
  std::string my_id;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::vector<identity::Contact> contacts;
  std::vector<messaging::ChatSummary> chats;
  std::optional<std::string> selected_contact_id;
  std::deque<messaging::HistoryEntryRecord> interaction_history;
  bool has_older_messages = false;
};

inline void to_json(nlohmann::json& j, const UIState& s) {
  j["my_id"] = s.my_id;
  j["connection_state"] = static_cast<int>(s.connection_state);
  j["contacts"] = s.contacts;
  j["chats"] = s.chats;
  j["selected_contact_id"] = s.selected_contact_id;
  j["interaction_history"] = s.interaction_history;
  j["has_older_messages"] = s.has_older_messages;
}

// MercuryViewModel encapsulates the application logic and state management for Mercury clients.
// It interacts with the RtcClient and provides a clean interface for any UI (CLI, GUI).
class MercuryViewModel {
public:
  explicit MercuryViewModel(std::shared_ptr<RtcClient> rtc_client)
      : rtc_client_(std::move(rtc_client)) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.my_id = rtc_client_->GetId();
      UpdateContacts();
    }
    UpdateChats();
    SetupRtcClientCallbacks();
  }
  ~MercuryViewModel() = default;

  MercuryViewModel(const MercuryViewModel&) = delete;
  MercuryViewModel& operator=(const MercuryViewModel&) = delete;
  MercuryViewModel(MercuryViewModel&&) = delete;
  MercuryViewModel& operator=(MercuryViewModel&&) = delete;

  // --- UI Actions ---
  void SelectContact(const std::string& peer_id);
  void DeselectContact();
  void SendMessage(const std::string& content);
  void StartVoiceCall();
  void StartVideoCall();
  void AddContact(const std::string& peer_id) const;
  void AcceptContact(const std::string& peer_id) const;
  void LoadOlderMessages();
  void RefreshState();

  // --- CLI Specific ---
  auto ProcessCliInput(const std::string& input) -> bool;
  void SelectContactByIndex(size_t index);


  // --- State Access ---
  [[nodiscard]] auto GetState() const -> UIState;
  void SetOnStateChangedCallback(std::function<void()> callback);

private:
  void SetupRtcClientCallbacks();
  void UpdateChatHistoryForSelectedContact();
  void AddSystemMessageToChat(const std::string& message);
  void NotifyStateChanged() const;
  void UpdateContacts();
  void UpdateChats();
  void LoadInitialChatHistory(const std::string& peer_id);

  std::shared_ptr<RtcClient> rtc_client_;

  mutable std::mutex state_mutex_;
  UIState current_state_;

  // Per-peer-ID state
  std::unordered_map<std::string, size_t> chat_pages_loaded_;
  DispatchQueue dispatch_queue_{"ViewModel", 1};

  static constexpr size_t kChatPageSize = 50;

  std::function<void()> on_state_changed_callback_;
};

// --- Implementation ---

inline void MercuryViewModel::SelectContact(const std::string& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    bool selection_changed{false};
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      selection_changed = current_state_.selected_contact_id != peer_id;
      if (selection_changed) {
        current_state_.selected_contact_id = peer_id;
        LoadInitialChatHistory(peer_id);
      }
    }
    if (selection_changed) {
      NotifyStateChanged();
    }
  });
}

inline void MercuryViewModel::DeselectContact() {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  if (current_state_.selected_contact_id.has_value()) {
    current_state_.selected_contact_id.reset();
    current_state_.interaction_history.clear();
    current_state_.has_older_messages = false;
    NotifyStateChanged();
  }
}

inline void MercuryViewModel::SendMessage(const std::string& content) {
  std::optional<std::string> selected_id;

  // Perform quick, optimistic state updates within a short-lived lock.
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    selected_id = current_state_.selected_contact_id;

    if (!selected_id) {
      LOGW << "SendMessage called with no selected contact.";
      return;
    }

    current_state_.interaction_history.push_back({
        .kind = messaging::HistoryEntryKind::TextMessage,
        .timestamp = Time::Now(),
        .direction = messaging::MessageDirection::sent,
        .data = messaging::MessageEntry{content}
    });
  }

  // Update chat summaries, which can be slow, outside the lock.
  UpdateChats();
  NotifyStateChanged();

  // Dispatch the actual sending logic to a background thread to avoid blocking the main thread.
  dispatch_queue_.Dispatch([this, peer_id = *selected_id, content] {
    rtc_client_->SendChatMessage(peer_id, content);
  });
}

inline void MercuryViewModel::StartVoiceCall() {
    dispatch_queue_.Dispatch([this] {
        std::optional<std::string> peer_id;
        {
            const std::lock_guard<std::mutex> lock(state_mutex_);
            peer_id = current_state_.selected_contact_id;
        }
        if (peer_id) {
            rtc_client_->StartVoiceCall(*peer_id);
            RefreshState();
        }
    });
}

inline void MercuryViewModel::StartVideoCall() {
    dispatch_queue_.Dispatch([this] {
        std::optional<std::string> peer_id;
        {
            const std::lock_guard<std::mutex> lock(state_mutex_);
            peer_id = current_state_.selected_contact_id;
        }
        if (peer_id) {
            rtc_client_->StartVideoCall(*peer_id);
            RefreshState();
        }
    });
}

inline void MercuryViewModel::AddContact(const std::string& peer_id) const {
  rtc_client_->AddContact(peer_id);
}

inline void MercuryViewModel::AcceptContact(const std::string& peer_id) const {
  rtc_client_->ConfirmContact(peer_id);
}

inline void MercuryViewModel::LoadOlderMessages() {
  dispatch_queue_.Dispatch([this] {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.selected_contact_id) {
        return;
      }

      const auto& peer_id = *current_state_.selected_contact_id;
      const size_t pages_loaded = chat_pages_loaded_[peer_id];

      auto page = rtc_client_->GetMessageHistoryPage(peer_id, pages_loaded, kChatPageSize);
      if (page.messages.empty()) {
        current_state_.has_older_messages = false;
      } else {
        for (auto it = page.messages.rbegin(); it != page.messages.rend(); ++it) {
          current_state_.interaction_history.push_front(*it);
        }
        chat_pages_loaded_[peer_id]++;
        current_state_.has_older_messages = (chat_pages_loaded_[peer_id] * kChatPageSize) < page.total_messages;
      }
    }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::RefreshState() {
  dispatch_queue_.Dispatch([this] {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.my_id = rtc_client_->GetId();
      UpdateContacts();
    }
    UpdateChats();
    NotifyStateChanged();
  });
}

inline auto MercuryViewModel::ProcessCliInput(const std::string& input) -> bool {
  if (input.empty()) {
    return false;
  }

  if (input.starts_with("/")) {
    std::stringstream ss(input);
    std::string command;
    std::string arg;
    ss >> command;
    std::getline(ss >> std::ws, arg);

    if (command == "/add") {
      AddSystemMessageToChat("--> Adding contact: " + arg);
      AddContact(arg);
    } else if (command == "/accept") {
      if (arg.empty()) {
        AddSystemMessageToChat("--> Usage: /accept <contact_id>");
      } else {
        std::vector<std::string> matches;
        {
          const std::lock_guard<std::mutex> lock(state_mutex_);
          for (const auto& contact : current_state_.contacts) {
            if (contact.peer_id.starts_with(arg)) matches.push_back(contact.peer_id);
          }
        }
        if (matches.empty()) {
          AddSystemMessageToChat("--> Contact not found for ID starting with: " + arg);
        }
        else if (matches.size() > 1) {
          AddSystemMessageToChat("--> Ambiguous ID. Multiple contacts start with: " + arg);
        }
        else {
          AddSystemMessageToChat("--> Accepting contact: " + matches[0]);
          AcceptContact(matches[0]);
        }
      }
    } else if (command == "/history" && arg == "load") {
      LoadOlderMessages();
    } else if (command == "/exit") {
      return true;
    } else {
      AddSystemMessageToChat("--> Unknown command: " + command);
    }
  } else {
    SendMessage(input);
  }
  NotifyStateChanged();
  return false;
}

inline void MercuryViewModel::SelectContactByIndex(size_t index) {
  std::string new_selection_id;
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    if (index < current_state_.contacts.size()) {
      new_selection_id = current_state_.contacts[index].peer_id;
    }
  }
  if (!new_selection_id.empty()) {
    SelectContact(new_selection_id);
  } else {
    DeselectContact();
  }
}

inline auto MercuryViewModel::GetState() const -> UIState {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return current_state_;
}

inline void MercuryViewModel::SetOnStateChangedCallback(std::function<void()> callback) {
  on_state_changed_callback_ = std::move(callback);
}

inline void MercuryViewModel::LoadInitialChatHistory(const std::string& peer_id) {
  // This must be called from a locked context.
  auto page = rtc_client_->GetMessageHistoryPage(peer_id, 0, kChatPageSize);
  current_state_.interaction_history.assign(page.messages.begin(), page.messages.end());
  chat_pages_loaded_[peer_id] = 1;
  current_state_.has_older_messages = (kChatPageSize < page.total_messages);
}

inline void MercuryViewModel::SetupRtcClientCallbacks() {
  rtc_client_->SetChatMessageReceivedCallback([this](const std::string& peer_id, const messaging::HistoryEntryRecord& message) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        current_state_.interaction_history.push_back(message);
      }
    }
    UpdateChats();
    NotifyStateChanged();
  });

  rtc_client_->SetContactRequestCallback([this](const std::string& peer_id) {
    {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        UpdateContacts();
        if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
          AddSystemMessageToChat("--> Contact request received. Use '/accept' to confirm.");
        }
    }
    NotifyStateChanged();
  });

  rtc_client_->SetContactStatusChangedCallback([this](const std::string& /*peer_id*/, identity::ContactStatus /*status*/) {
    RefreshState();
  });

  rtc_client_->SetStatusCallback([this](const std::string& /*peer_id*/, EventSource source, int state_val) {
    if (source == EventSource::system) {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.connection_state = static_cast<ServiceConnectionState>(state_val);
      }
      NotifyStateChanged();
    }
  });

  // Callbacks for call history would be added here
}

inline void MercuryViewModel::UpdateChatHistoryForSelectedContact() {
  // This must be called from within a locked context.
  if (current_state_.selected_contact_id) {
    const auto& peer_id = *current_state_.selected_contact_id;
    if (!chat_pages_loaded_.contains(peer_id)) {
      LoadInitialChatHistory(peer_id);
    }
  } else {
    current_state_.interaction_history.clear();
    current_state_.has_older_messages = false;
  }
}

inline void MercuryViewModel::AddSystemMessageToChat(const std::string& message) {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  current_state_.interaction_history.push_back({
      .kind = messaging::HistoryEntryKind::SystemMessage,
      .timestamp = Time::Now(),
      .direction = messaging::MessageDirection::received,
      .data = messaging::MessageEntry{message}
  });
}

inline void MercuryViewModel::NotifyStateChanged() const {
  if (on_state_changed_callback_) {
    on_state_changed_callback_();
  }
}

inline void MercuryViewModel::UpdateContacts() {
  // Must be called from a locked context
  current_state_.contacts = rtc_client_->GetContacts();
}

inline void MercuryViewModel::UpdateChats() {
  std::vector<std::string> contact_ids;
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    contact_ids.reserve(current_state_.contacts.size());
    for (const auto& contact : current_state_.contacts) {
      contact_ids.push_back(contact.peer_id);
    }
  }

  // Get summaries outside the lock to avoid holding it during I/O.
  auto new_chats = rtc_client_->GetChatSummaries(contact_ids);

  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    current_state_.chats = std::move(new_chats);
  }
}

}  // namespace eillance::view_model
