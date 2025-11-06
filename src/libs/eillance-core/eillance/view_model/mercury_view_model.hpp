#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/messaging/history.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance::view_model {

NLOHMANN_JSON_SERIALIZE_ENUM(eillance::transport::PeerConnectionState, {  // NOLINT
  {transport::PeerConnectionState::NewConnection, 0},
  {transport::PeerConnectionState::Connecting, 1},
  {transport::PeerConnectionState::Connected, 2},
  {transport::PeerConnectionState::Disconnected, 3},
  {transport::PeerConnectionState::Failed, 4},
  {transport::PeerConnectionState::Closed, 5},
})

struct UIState {
  struct CallState {
    bool is_in_call = false;
    bool is_ringing = false;
    messaging::CallDirection call_direction = messaging::CallDirection::outgoing;
    std::string peer_id;
    messaging::HistoryEntryKind call_type = messaging::HistoryEntryKind::voiceCall;
  };

  bool identity_exists = true;

  struct ViewContact {
    std::string peer_id;
    std::string display_name;
    std::optional<std::string> bio;
    std::optional<std::string> photo; // base64
    identity::ContactStatus status;
    eillance::transport::PeerConnectionState connection_state = eillance::transport::PeerConnectionState::Disconnected;
    uint32_t unread_count = 0;
  };

  std::string my_id;
  std::optional<std::string> my_display_name;
  std::optional<std::string> my_bio;
  std::optional<std::string> my_photo; // base64
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::vector<ViewContact> contacts;
  std::vector<messaging::ChatSummary> chats;
  std::optional<std::string> selected_contact_id;
  std::deque<messaging::HistoryEntryRecord> interaction_history;
  bool has_older_messages = false;
  CallState call_state;
  std::optional<std::string> signaling_server_url;
};

//NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(nlohmann::json& json, const UIState::ViewContact& c) {
  json = nlohmann::json{
      {"peer_id", c.peer_id},
      {"name", c.display_name}, // Mapped to 'name' for Kotlin consistency
      {"bio", c.bio},
      {"photo", c.photo},
      {"status", c.status},
      {"connection_state", c.connection_state},
      {"unread_count", c.unread_count},
  };
}

//NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(nlohmann::json& j, const std::deque<messaging::HistoryEntryRecord>& deq) {
  j = nlohmann::json::array();
  for (const auto& msg : deq) {
    j.push_back(msg);
  }
}

//NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(nlohmann::json& json, const UIState& s) {
  json["my_id"] = s.my_id;
  json["identity_exists"] = s.identity_exists;
  json["myDisplayName"] = s.my_display_name;
  json["myBio"] = s.my_bio;
  json["myPhoto"] = s.my_photo;
  json["connection_state"] = static_cast<int>(s.connection_state);
  json["contacts"] = s.contacts;
  json["chats"] = s.chats;
  json["selected_contact_id"] = s.selected_contact_id;
  json["interaction_history"] = s.interaction_history;
  json["has_older_messages"] = s.has_older_messages;
  json["call_state"]["is_in_call"] = s.call_state.is_in_call;
  json["call_state"]["is_ringing"] = s.call_state.is_ringing;
  json["call_state"]["call_direction"] = s.call_state.call_direction;
  json["call_state"]["peer_id"] = s.call_state.peer_id;
  json["call_state"]["call_type"] = s.call_state.call_type;
  json["signaling_server_url"] = s.signaling_server_url;
}

class MercuryViewModel {
public:
  explicit MercuryViewModel(std::shared_ptr<Client> client) : client_(std::move(client)) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.identity_exists = client_->IdentityExists();
      if (!current_state_.identity_exists) return;
      current_state_.my_id = client_->getId();
      UpdateMyProfileFromSettings();
      current_state_.signaling_server_url = client_->getSettingsManager()->GetSignalingServerUrl();
      UpdateContacts();
    }
    UpdateChats();
    SetupClientCallbacks();
  }
  ~MercuryViewModel() = default;

  MercuryViewModel(const MercuryViewModel&) = delete;
  auto operator=(const MercuryViewModel&) -> MercuryViewModel& = delete;
  MercuryViewModel(MercuryViewModel&&) = delete;
  auto operator=(MercuryViewModel&&) -> MercuryViewModel& = delete;

  auto GenerateNewIdentity() -> std::string;
  auto RestoreIdentity(const std::string& mnemonic) -> bool;
  auto GetMnemonic() -> std::optional<std::string>;

  auto CreateBackup(const std::string& path, const std::string& password) -> bool;
  auto RestoreBackup(const std::string& path, const std::string& password) -> bool;

  void SelectContact(const std::string& peer_id);
  void DeselectContact();
  void SendMessage(const std::string& content);
  void StartVoiceCall();
  void StartVideoCall();
  void Hangup();
  void AcceptCall();
  void RejectCall();
  void RemoveContact(const std::string& peer_id);
  void AddContact(const std::string& peer_id);
  void UpdateSignalingUrl(const std::string& url);
  void AcceptContact(const std::string& peer_id);
  void UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo);
  void SetContactDisplayNameOverride(const std::string& peer_id, const std::string& name);
  void ResetContactDisplayNameOverride(const std::string& peer_id);
  void LoadOlderMessages();
  void LoadFullMessage(uint64_t message_id);
  void RefreshState();
  void MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids);
  void AddSystemMessageToChat(const std::string& message);
  void SelectContactByIndex(size_t index);
  void DebugEraseAllHistory();
  void DebugEraseAllContacts();
  void DebugRevokeAllApprovals();
  [[nodiscard]] auto GetState() const -> UIState;
  void SetOnStateChangedCallback(std::function<void()> callback);

private:
  void SetupClientCallbacks();
  void UpdateChatHistoryForSelectedContact();
  void NotifyStateChanged() const;
  void UpdateContacts();
  void UpdateMyProfileFromSettings();
  void UpdateChats();
  void LoadInitialChatHistory(const std::string& peer_id);

  static inline std::atomic<std::uint64_t> nextSystemMessageId{10000000000000000000ULL};
  std::shared_ptr<Client> client_;

  mutable std::mutex state_mutex_;
  UIState current_state_;

  std::unordered_map<std::string, size_t> chat_pages_loaded_;
  DispatchQueue dispatch_queue_{"ViewModel", 1};

  static constexpr size_t kChatPageSize = 50;

  std::function<void()> on_state_changed_callback_;
};

inline auto MercuryViewModel::GenerateNewIdentity() -> std::string {
    auto mnemonic = client_->GenerateNewIdentity();
    if (!mnemonic.empty()) {
        dispatch_queue_.Dispatch([this] {
            RefreshState();
        });
    }
    return mnemonic;
}

inline auto MercuryViewModel::RestoreIdentity(const std::string& mnemonic) -> bool {
    if (client_->RestoreIdentity(mnemonic)) {
        dispatch_queue_.Dispatch([this] {
            RefreshState();
        });
        return true;
    }
    return false;
}

inline auto MercuryViewModel::GetMnemonic() -> std::optional<std::string> {
    return client_->GetMnemonic();
}

inline auto MercuryViewModel::CreateBackup(const std::string& path, const std::string& password) -> bool {
    return client_->CreateBackup(path, password);
}

inline auto MercuryViewModel::RestoreBackup(const std::string& path, const std::string& password) -> bool {
    return client_->RestoreBackup(path, password);
}

inline void MercuryViewModel::SelectContact(const std::string& peer_id) {
  const auto state = client_->getPeerConnectionState(peer_id);
  if (state == transport::PeerConnectionState::Disconnected
      || state == transport::PeerConnectionState::Failed
      || state == transport::PeerConnectionState::Closed
      || state == transport::PeerConnectionState::NewConnection) {
    client_->connect(peer_id);
  }

  dispatch_queue_.Dispatch([this, peer_id]() -> void {
    bool selection_changed{false};
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      selection_changed = current_state_.selected_contact_id != peer_id;
      if (selection_changed) {
        current_state_.selected_contact_id = peer_id;
        LoadInitialChatHistory(peer_id);
      }
    }
    if (selection_changed) { NotifyStateChanged(); }
  });
}

inline void MercuryViewModel::DeselectContact() {
  dispatch_queue_.Dispatch([this] {
    bool changed = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (current_state_.selected_contact_id.has_value()) {
        current_state_.selected_contact_id.reset();
        current_state_.interaction_history.clear();
        current_state_.has_older_messages = false;
        changed = true;
      }
    }
    if (changed) {
      NotifyStateChanged();
    }
  });
}

inline void MercuryViewModel::SendMessage(const std::string& content) {
  dispatch_queue_.Dispatch([this, content] {
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
      if (!current_state_.call_state.is_in_call && !current_state_.call_state.is_ringing) { return; }
      peer_id_to_hangup = current_state_.call_state.peer_id;
      current_state_.call_state = {};
    }
    if (!peer_id_to_hangup.empty()) { client_->hangup(peer_id_to_hangup); }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::AcceptCall() {
  dispatch_queue_.Dispatch([this] {
    std::string peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.call_state.is_ringing) { return; }
      peer_id = current_state_.call_state.peer_id;
      current_state_.call_state.is_in_call = true;
      current_state_.call_state.is_ringing = false;
    }
    if (!peer_id.empty()) { client_->acceptCall(peer_id); }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::RejectCall() {
  dispatch_queue_.Dispatch([this] {
    std::string peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.call_state.is_ringing) { return; }
      peer_id = current_state_.call_state.peer_id;
      current_state_.call_state = {};
    }
    if (!peer_id.empty()) { client_->rejectCall(peer_id); }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::AddContact(const std::string& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] { client_->addContact(peer_id); });
}

inline void MercuryViewModel::UpdateSignalingUrl(const std::string& url) {
  dispatch_queue_.Dispatch([this, url] { client_->UpdateSignalingUrl(url); });
}

inline void MercuryViewModel::RemoveContact(const std::string& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->removeContact(peer_id);
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        current_state_.selected_contact_id.reset();
        current_state_.interaction_history.clear();
        current_state_.has_older_messages = false;
      }
      UpdateContacts();
    }
    UpdateChats();
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::AcceptContact(const std::string& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] { client_->confirmContact(peer_id); });
}

inline void MercuryViewModel::UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) {
  dispatch_queue_.Dispatch([this, name, bio, photo] {
    client_->updateMyProfile(name, bio, photo);
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      UpdateMyProfileFromSettings();
    }
    NotifyStateChanged();
  });
}

inline void MercuryViewModel::SetContactDisplayNameOverride(const std::string& peer_id, const std::string& name) {
  dispatch_queue_.Dispatch([this, peer_id, name] {
    client_->setContactDisplayNameOverride(peer_id, name);
    RefreshState(); // Easiest way to update the view model's contact list
  });
}

inline void MercuryViewModel::ResetContactDisplayNameOverride(const std::string& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->resetContactDisplayNameOverride(peer_id);
    RefreshState();
  });
}

inline void MercuryViewModel::LoadOlderMessages() {
  dispatch_queue_.Dispatch([this] {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.selected_contact_id) { return; }

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

inline void MercuryViewModel::LoadFullMessage(uint64_t message_id) {
  dispatch_queue_.Dispatch([this, message_id] {
    auto full_content = client_->getFullMessageContent(message_id);
    if (!full_content) {
      LOGW << "Could not load full content for message ID: " << message_id;
      return;
    }

    bool updated = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (current_state_.selected_contact_id) {
        for (auto& entry : current_state_.interaction_history) {
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

    if (updated) { NotifyStateChanged(); }
  });
}

inline void MercuryViewModel::RefreshState() {
  dispatch_queue_.Dispatch([this] {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.identity_exists = client_->IdentityExists();
      current_state_.my_id = client_->getId();
      UpdateMyProfileFromSettings();
      current_state_.signaling_server_url = client_->getSettingsManager()->GetSignalingServerUrl();
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
      // After marking messages as read in the database, the chat summaries
      // must be recalculated to reflect the new unread counts.
      UpdateChats();
      NotifyStateChanged();
    }
  });
}

inline void MercuryViewModel::AddSystemMessageToChat(const std::string& message) {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  current_state_.interaction_history.push_back({.id = nextSystemMessageId++, .kind = messaging::HistoryEntryKind::systemMessage, .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()), .direction = messaging::MessageDirection::received, .is_truncated = false, .data = messaging::MessageEntry{message}});
  NotifyStateChanged();
}

inline void MercuryViewModel::SelectContactByIndex(size_t index) {
  std::string new_selection_id;
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    if (index < current_state_.contacts.size()) { new_selection_id = current_state_.contacts[index].peer_id; }
  }
  if (!new_selection_id.empty()) {
    SelectContact(new_selection_id);
  } else {
    DeselectContact();
  }
}

inline void MercuryViewModel::DebugEraseAllHistory() {
  dispatch_queue_.Dispatch([this] {
    client_->DebugEraseAllHistory();
    RefreshState();
  });
}

inline void MercuryViewModel::DebugEraseAllContacts() {
  dispatch_queue_.Dispatch([this] {
    client_->DebugEraseAllContacts();
    RefreshState();
  });
}

inline void MercuryViewModel::DebugRevokeAllApprovals() {
  dispatch_queue_.Dispatch([this] {
    client_->DebugRevokeAllApprovals();
    RefreshState();
  });
}

inline auto MercuryViewModel::GetState() const -> UIState {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return current_state_;
}

inline void MercuryViewModel::SetOnStateChangedCallback(std::function<void()> callback) { on_state_changed_callback_ = std::move(callback); }

inline void MercuryViewModel::LoadInitialChatHistory(const std::string& peer_id) {
  auto page = client_->getMessageHistoryPage(peer_id, 0, kChatPageSize);
  current_state_.interaction_history.assign(page.messages.begin(), page.messages.end());
  chat_pages_loaded_[peer_id] = 1;
  current_state_.has_older_messages = (kChatPageSize < page.total_messages);
}

inline void MercuryViewModel::SetupClientCallbacks() {
  client_->setChatMessageReceivedCallback([this](const std::string& peer_id,
                                                 const messaging::HistoryEntryRecord& message) {
    dispatch_queue_.Dispatch([this, peer_id, message] {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
          current_state_.interaction_history.push_back(message);
        }
      }
      UpdateChats();
      NotifyStateChanged();
    });
  });

  client_->setContactRequestCallback([this](const std::string& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        UpdateContacts();
        if (current_state_.selected_contact_id &&
            *current_state_.selected_contact_id == peer_id) {
          AddSystemMessageToChat("--> Contact request received. Use '/accept' to confirm.");
        }
      }
      NotifyStateChanged();
    });
  });

  client_->setOnContactProfileUpdatedCallback([this](const std::string& /*peer_id*/) {
    dispatch_queue_.Dispatch([this] {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        UpdateContacts();
      }
      NotifyStateChanged();
    });
  });

  client_->setContactStatusChangedCallback(
      [this](const std::string& /*peer_id*/, identity::ContactStatus /*status*/) { RefreshState(); });

  client_->setStatusCallback([this](const std::string& peer_id, EventSource source, int state_val) {
    dispatch_queue_.Dispatch([this, peer_id, source, state_val] {
      bool state_changed = false;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (source == EventSource::system) {
          auto new_state = static_cast<ServiceConnectionState>(state_val);
          if (current_state_.connection_state != new_state) {
            current_state_.connection_state = new_state;
            if (current_state_.connection_state == ServiceConnectionState::online) {
              current_state_.signaling_server_url =
                  client_->getSettingsManager()->GetSignalingServerUrl();
            }
            state_changed = true;
          }
        } else if (source == EventSource::peerConnectionState) {
          const auto state = static_cast<eillance::transport::PeerConnectionState>(state_val);
          for (auto& contact : current_state_.contacts) {
            if (contact.peer_id == peer_id) {
              if (contact.connection_state != state) {
                contact.connection_state = state;
                state_changed = true;
              }
              break;
            }
          }
          if (state == transport::PeerConnectionState::Closed ||
              state == transport::PeerConnectionState::Disconnected ||
              state == transport::PeerConnectionState::Failed) {
            if ((current_state_.call_state.is_in_call || current_state_.call_state.is_ringing) &&
                current_state_.call_state.peer_id == peer_id) {
              current_state_.call_state = {};
              state_changed = true;
            }
          }
        }
      }
      if (state_changed) { NotifyStateChanged(); }
    });
  });

  client_->setIncomingCallCallback([this](const std::string& peer_id, messaging::HistoryEntryKind call_type) {
    dispatch_queue_.Dispatch([this, peer_id, call_type] {
      bool state_changed = false;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (!current_state_.call_state.is_in_call && !current_state_.call_state.is_ringing) {
          current_state_.call_state.is_ringing = true;
          current_state_.call_state.peer_id = peer_id;
          current_state_.call_state.call_type = call_type;
          current_state_.call_state.call_direction = messaging::CallDirection::incoming;
          state_changed = true;
        }
      }
      if (state_changed) { NotifyStateChanged(); }
    });
  });

  client_->setOnCallTerminatedCallback([this](const std::string& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      bool state_changed = false;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if ((current_state_.call_state.is_in_call || current_state_.call_state.is_ringing) &&
            current_state_.call_state.peer_id == peer_id) {
          current_state_.call_state = {};  // Reset the call state
          state_changed = true;
        }
      }
      if (state_changed) { NotifyStateChanged(); }
    });
  });

  client_->setMessageStatusChangedCallback(
      [this](const std::string& peer_id, const std::uint64_t message_id,
             const messaging::MessageStatus status) {
        dispatch_queue_.Dispatch([this, peer_id, message_id, status] {
          bool changed = false;
          {
            const std::lock_guard<std::mutex> lock(state_mutex_);
            if (current_state_.selected_contact_id &&
                *current_state_.selected_contact_id == peer_id) {
              for (auto& entry : current_state_.interaction_history) {
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
          if (changed) { NotifyStateChanged(); }
        });
      });
}

inline void MercuryViewModel::UpdateChatHistoryForSelectedContact() {
  if (current_state_.selected_contact_id) {
    const auto& peer_id = *current_state_.selected_contact_id;
    if (!chat_pages_loaded_.contains(peer_id)) { LoadInitialChatHistory(peer_id); }
  } else {
    current_state_.interaction_history.clear();
    current_state_.has_older_messages = false;
  }
}

inline void MercuryViewModel::NotifyStateChanged() const {
  if (on_state_changed_callback_) { on_state_changed_callback_(); }
}

inline void MercuryViewModel::UpdateContacts() {
  auto contacts_from_manager = client_->getContacts();
  current_state_.contacts.clear();
  for (const auto& c : contacts_from_manager) {
    const std::string display_name = c.display_name_override.value_or(c.display_name.value_or(c.peer_id));
    current_state_.contacts.push_back({.peer_id = c.peer_id, .display_name = display_name, .bio = c.bio, .photo = c.photo_base64, .status = c.status, .connection_state = client_->getPeerConnectionState(c.peer_id), .unread_count = 0});
  }
}

inline void MercuryViewModel::UpdateMyProfileFromSettings() {
  const auto* settings = client_->getSettingsManager();
  current_state_.my_display_name = settings->GetMyDisplayName();
  current_state_.my_bio = settings->GetMyBio();
  current_state_.my_photo = settings->GetMyPhotoBase64();
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

    std::unordered_map<std::string, uint32_t> unread_counts;
    for (const auto& chat : new_chats) {
      unread_counts[chat.peer_id] = chat.unread_count;
    }

    for (auto& contact : current_state_.contacts) {
      if (unread_counts.contains(contact.peer_id)) {
        contact.unread_count = unread_counts.at(contact.peer_id);
      } else {
        contact.unread_count = 0;
      }
    }

    current_state_.chats = std::move(new_chats);
  }
}

}  // namespace eillance::view_model```
