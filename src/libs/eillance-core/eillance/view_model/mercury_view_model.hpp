#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <atomic>
#include <unordered_map>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance::view_model {
NLOHMANN_JSON_SERIALIZE_ENUM(eillance::transport::PeerConnectionState, {  // NOLINT
  {eillance::transport::PeerConnectionState::New, 0},
  {eillance::transport::PeerConnectionState::Connecting, 1},
  {eillance::transport::PeerConnectionState::Connected, 2},
  {eillance::transport::PeerConnectionState::Disconnected, 3},
  {eillance::transport::PeerConnectionState::Failed, 4},
  {eillance::transport::PeerConnectionState::Closed, 5},
})

struct UIState {
  struct CallState {
    bool is_in_call = false;
    bool is_ringing = false;
    messaging::CallDirection call_direction = messaging::CallDirection::outgoing;
    std::string peer_id;
    messaging::HistoryEntryKind call_type = messaging::HistoryEntryKind::voiceCall;
  };

  struct ViewContact {
    std::string peer_id;
    identity::ContactStatus status;
    eillance::transport::PeerConnectionState connection_state = eillance::transport::PeerConnectionState::Disconnected;
  };

  std::string my_id;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::vector<ViewContact> contacts;
  std::vector<messaging::ChatSummary> chats;
  std::optional<std::string> selected_contact_id;
  std::deque<messaging::HistoryEntryRecord> interaction_history;
  bool has_older_messages = false;
  CallState call_state;
};

inline void to_json(nlohmann::json& j, const UIState::ViewContact& c) {
  j = nlohmann::json{
      {"peer_id", c.peer_id},
      {"status", c.status},
      {"connection_state", c.connection_state}
  };
}

inline void to_json(nlohmann::json& j, const UIState& s) {
  j["my_id"] = s.my_id;
  j["connection_state"] = static_cast<int>(s.connection_state);
  j["contacts"] = s.contacts;
  j["chats"] = s.chats;
  j["selected_contact_id"] = s.selected_contact_id;
  j["interaction_history"] = s.interaction_history;
  j["has_older_messages"] = s.has_older_messages;
  j["call_state"]["is_in_call"] = s.call_state.is_in_call;
  j["call_state"]["is_ringing"] = s.call_state.is_ringing;
  j["call_state"]["call_direction"] = s.call_state.call_direction;
  j["call_state"]["peer_id"] = s.call_state.peer_id;
  j["call_state"]["call_type"] = s.call_state.call_type;
}

class MercuryViewModel {
public:
  explicit MercuryViewModel(std::shared_ptr<Client> client)
      : client_(std::move(client)) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.my_id = client_->getId();
      UpdateContacts();
    }
    UpdateChats();
    SetupClientCallbacks();
  }
  ~MercuryViewModel() = default;

  MercuryViewModel(const MercuryViewModel&) = delete;
  MercuryViewModel& operator=(const MercuryViewModel&) = delete;
  MercuryViewModel(MercuryViewModel&&) = delete;
  MercuryViewModel& operator=(MercuryViewModel&&) = delete;

  void SelectContact(const std::string& peer_id);
  void DeselectContact();
  void SendMessage(const std::string& content);
  void StartVoiceCall();
  void StartVideoCall();
  void Hangup();
  void AcceptCall();
  void RejectCall();
  void AddContact(const std::string& peer_id) const;
  void AcceptContact(const std::string& peer_id) const;
  void LoadOlderMessages();
  void RefreshState();
  void MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids);
  auto ProcessCliInput(const std::string& input) -> bool;
  void SelectContactByIndex(size_t index);
  [[nodiscard]] auto GetState() const -> UIState;
  void SetOnStateChangedCallback(std::function<void()> callback);

private:
  void SetupClientCallbacks();
  void UpdateChatHistoryForSelectedContact();
  void AddSystemMessageToChat(const std::string& message);
  void NotifyStateChanged() const;
  void UpdateContacts();
  void UpdateChats();
  void LoadInitialChatHistory(const std::string& peer_id);

  static inline std::atomic<std::uint64_t> next_system_message_id_{10000000000000000000ULL};
  std::shared_ptr<Client> client_;

  mutable std::mutex state_mutex_;
  UIState current_state_;

  std::unordered_map<std::string, size_t> chat_pages_loaded_;
  DispatchQueue dispatch_queue_{"ViewModel", 1};

  static constexpr size_t kChatPageSize = 50;

  std::function<void()> on_state_changed_callback_;
};

inline void MercuryViewModel::SelectContact(const std::string& peer_id) {
  const auto state = client_->getPeerConnectionState(peer_id);
  if (state == eillance::transport::PeerConnectionState::Disconnected ||
      state == eillance::transport::PeerConnectionState::Failed ||
      state == eillance::transport::PeerConnectionState::Closed ||
      state == eillance::transport::PeerConnectionState::New) {
    client_->connect(peer_id);
  }

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
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    selected_id = current_state_.selected_contact_id;
    if (!selected_id) {
      LOGW << "SendMessage called with no selected contact.";
      return;
    }
  }

  auto entry = client_->sendChatMessage(*selected_id, content);

  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    if (current_state_.selected_contact_id == selected_id) {
      current_state_.interaction_history.push_back(entry);
    }
  }

  UpdateChats();
  NotifyStateChanged();
}

inline void MercuryViewModel::StartVoiceCall() {
  dispatch_queue_.Dispatch([this] {
    std::optional<std::string> peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      peer_id = current_state_.selected_contact_id;
    }
    if (peer_id) {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.call_state = {.is_in_call = true, .is_ringing = false, .call_direction = messaging::CallDirection::outgoing, .peer_id = *peer_id, .call_type = messaging::HistoryEntryKind::voiceCall};
      }
      client_->startVoiceCall(*peer_id);
      UpdateChats();
      NotifyStateChanged();
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
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        current_state_.call_state = {.is_in_call = true, .is_ringing = false, .call_direction = messaging::CallDirection::outgoing, .peer_id = *peer_id, .call_type = messaging::HistoryEntryKind::videoCall};
      }
      client_->startVideoCall(*peer_id);
      UpdateChats();
      NotifyStateChanged();
    }
  });
}

inline void MercuryViewModel::Hangup() {
  dispatch_queue_.Dispatch([this] {
    std::string peer_id_to_hangup;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.call_state.is_in_call && !current_state_.call_state.is_ringing) return;
      peer_id_to_hangup = current_state_.call_state.peer_id;
      current_state_.call_state = {};
    }
    if (!peer_id_to_hangup.empty()) {
      client_->hangup(peer_id_to_hangup);
    }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::AcceptCall() {
  dispatch_queue_.Dispatch([this] {
    std::string peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.call_state.is_ringing) return;
      peer_id = current_state_.call_state.peer_id;
      current_state_.call_state.is_in_call = true;
      current_state_.call_state.is_ringing = false;
    }
    if (!peer_id.empty()) {
      client_->acceptCall(peer_id);
    }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::RejectCall() {
  dispatch_queue_.Dispatch([this] {
    std::string peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.call_state.is_ringing) return;
      peer_id = current_state_.call_state.peer_id;
      current_state_.call_state = {};
    }
    if (!peer_id.empty()) {
      client_->rejectCall(peer_id);
    }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::AddContact(const std::string& peer_id) const {
  client_->addContact(peer_id);
}

inline void MercuryViewModel::AcceptContact(const std::string& peer_id) const {
  client_->confirmContact(peer_id);
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

      auto page = client_->getMessageHistoryPage(peer_id, pages_loaded, kChatPageSize);
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
      current_state_.my_id = client_->getId();
      UpdateContacts();
    }
    UpdateChats();
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids) {
  dispatch_queue_.Dispatch([this, message_ids] {
    std::optional<std::string> peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      peer_id = current_state_.selected_contact_id;
    }
    if (peer_id && !message_ids.empty()) {
      LOGI << "Marking " << message_ids.size() << " messages as read for peer " << *peer_id;
      client_->markMessagesAsRead(*peer_id, message_ids);
    }
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
        } else if (matches.size() > 1) {
          AddSystemMessageToChat("--> Ambiguous ID. Multiple contacts start with: " + arg);
        } else {
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
  auto page = client_->getMessageHistoryPage(peer_id, 0, kChatPageSize);
  current_state_.interaction_history.assign(page.messages.begin(), page.messages.end());
  chat_pages_loaded_[peer_id] = 1;
  current_state_.has_older_messages = (kChatPageSize < page.total_messages);
}

inline void MercuryViewModel::SetupClientCallbacks() {
  client_->setChatMessageReceivedCallback([this](const std::string& peer_id, const messaging::HistoryEntryRecord& message) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        current_state_.interaction_history.push_back(message);
      }
    }
    UpdateChats();
    NotifyStateChanged();
  });

  client_->setContactRequestCallback([this](const std::string& peer_id) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      UpdateContacts();
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        AddSystemMessageToChat("--> Contact request received. Use '/accept' to confirm.");
      }
    }
    NotifyStateChanged();
  });

  client_->setContactStatusChangedCallback([this](const std::string&, identity::ContactStatus) {
    RefreshState();
  });

  client_->setStatusCallback([this](const std::string& peer_id, EventSource source, int state_val) {
    bool state_changed = false;
    if (source == EventSource::system) {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.connection_state = static_cast<ServiceConnectionState>(state_val);
      state_changed = true;
    } else if (source == EventSource::peerConnectionState) {
      const auto state = static_cast<eillance::transport::PeerConnectionState>(state_val);
      const std::lock_guard<std::mutex> lock(state_mutex_);
      for (auto& contact : current_state_.contacts) {
        if (contact.peer_id == peer_id) {
          if (contact.connection_state != state) {
            contact.connection_state = state;
            state_changed = true;
          }
          break;
        }
      }
      if (state == eillance::transport::PeerConnectionState::Closed ||
          state == eillance::transport::PeerConnectionState::Disconnected ||
          state == eillance::transport::PeerConnectionState::Failed) {
        if ((current_state_.call_state.is_in_call || current_state_.call_state.is_ringing) && current_state_.call_state.peer_id == peer_id) {
          current_state_.call_state = {};
          state_changed = true;
        }
      }
    }
    if (state_changed) {
      NotifyStateChanged();
    }
  });

  client_->setIncomingCallCallback([this](const std::string& peer_id, messaging::HistoryEntryKind call_type) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (current_state_.call_state.is_in_call || current_state_.call_state.is_ringing) {
        return;
      }
      current_state_.call_state.is_ringing = true;
      current_state_.call_state.peer_id = peer_id;
      current_state_.call_state.call_type = call_type;
      current_state_.call_state.call_direction = messaging::CallDirection::incoming;
    }
    NotifyStateChanged();
  });

  client_->setOnCallTerminatedCallback([this](const std::string& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
        bool state_changed = false;
        {
            const std::lock_guard<std::mutex> lock(state_mutex_);
            if ((current_state_.call_state.is_in_call || current_state_.call_state.is_ringing) && current_state_.call_state.peer_id == peer_id) {
                current_state_.call_state = {}; // Reset the call state
                state_changed = true;
            }
        }
        if (state_changed) {
            NotifyStateChanged();
        }
    });
  });

  client_->setMessageStatusChangedCallback(
      [this](const std::string& peer_id, const std::uint64_t message_id,
             const messaging::MessageStatus status) {
        {
          const std::lock_guard<std::mutex> lock(state_mutex_);
          if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
            for (auto& entry : current_state_.interaction_history) {
              if (entry.id == message_id) {
                entry.status = status;
                break;
              }
            }
          }
        }
        NotifyStateChanged();
      });
}

inline void MercuryViewModel::UpdateChatHistoryForSelectedContact() {
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
                                                   .id = next_system_message_id_++,
                                                   .kind = messaging::HistoryEntryKind::systemMessage,
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
  auto contacts_from_manager = client_->getContacts();
  current_state_.contacts.clear();
  for (const auto& c : contacts_from_manager) {
    current_state_.contacts.push_back({
                                          c.peer_id,
                                          c.status,
                                          client_->getPeerConnectionState(c.peer_id)
                                      });
  }
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
  {
    auto new_chats = client_->getChatSummaries(contact_ids);
    const std::lock_guard<std::mutex> lock(state_mutex_);
    current_state_.chats = std::move(new_chats);
  }
}

} // namespace eillance::view_model
