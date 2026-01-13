#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
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

#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/peer.hpp"
#include "eillance/transport/peer_connection_state.h"
#include "eillance/utils/dispatch_queue.hpp"

namespace eillance::view_model {

// This struct is now primarily for the CLI client's convenience.
// The Android UI queries the ViewModel through granular getters.
struct UIState {
  struct CallState {
    bool is_connecting = false;
    bool is_in_call = false;
    bool is_ringing = false;
    messaging::CallDirection call_direction = messaging::CallDirection::outgoing;
    ContactID peer_id;
    messaging::HistoryEntryKind call_type = messaging::HistoryEntryKind::voiceCall;
  };

  bool identity_exists = true;

  struct ViewContact {
    ContactID peer_id;
    std::string display_name;
    std::optional<std::string> bio;
    std::optional<std::string> photo;  // base64
    identity::ContactStatus status;
    eillance::transport::PeerConnectionState connection_state = eillance::transport::PeerConnectionState::Disconnected;
    uint32_t unread_count = 0;
  };

  struct MyProfile {
    ContactID my_id;
    bool identity_exists = true;
    std::optional<std::string> my_display_name;
    std::optional<std::string> my_bio;
    std::optional<std::string> my_photo;  // base64
  };

  MyProfile my_profile;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::vector<ViewContact> contacts;
  std::optional<ContactID> selected_contact_id;
  std::deque<messaging::HistoryEntryRecord> interaction_history;
  bool has_older_messages = false;
  CallState call_state;
  std::optional<std::string> signaling_server_url;
};

enum class StateChange {
  MyProfile,
  Contacts,
  Connection,
  SelectedContact,
  InteractionHistory,
  CallState,
  SignalingUrl
};

class MercuryViewModel {
public:
  explicit MercuryViewModel(std::shared_ptr<Client> client);
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

  void SelectContact(const ContactID& peer_id);
  void DeselectContact();
  void SendMessage(const std::string& content);
  void StartVoiceCall();
  void StartVideoCall();
  void Hangup();
  void AcceptCall();
  void RejectCall();
  void BlockContact(const ContactID& peer_id);
  void DeleteContact(const ContactID& peer_id);
  void UnblockContact(const ContactID& peer_id);
  void AddContact(const ContactID& peer_id, const std::optional<std::string>& token = std::nullopt);
  auto GenerateContactToken() -> std::string;
  void UpdateSignalingUrl(const std::string& url);
  void AcceptContact(const ContactID& peer_id);
  void UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo);
  void SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& name);
  void ResetContactDisplayNameOverride(const ContactID& peer_id);
  void LoadOlderMessages();
  void LoadFullMessage(uint64_t message_id);
  void RefreshState();
  void MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids);
  void AddSystemMessageToChat(const std::string& message);
  void SelectContactByIndex(size_t index);
  void DebugEraseAllHistory();
  void DebugEraseAllContacts();
  void DebugRevokeAllApprovals();

  /// Returns a full copy of the current state. Used for CLI and initial state hydration.
  [[nodiscard]] auto GetState() const -> UIState;

  // --- Granular Getters for Android UI ---
  [[nodiscard]] auto GetMyProfile() const -> UIState::MyProfile;
  [[nodiscard]] auto GetContacts() const -> std::vector<UIState::ViewContact>;
  [[nodiscard]] auto GetInteractionHistory() const -> std::deque<messaging::HistoryEntryRecord>;
  [[nodiscard]] auto HasOlderMessages() const -> bool;
  [[nodiscard]] auto GetCallState() const -> UIState::CallState;

  void SetOnStateChangedCallback(std::function<void(const std::vector<StateChange>&)> callback);

private:
  void SetupClientCallbacks();
  void NotifyStateChanged(const std::vector<StateChange>& changes) const;
  void UpdateContacts(bool notify);
  void UpdateMyProfileFromSettings(bool notify);
  void UpdateChats(bool notify);
  void LoadInitialChatHistory(const ContactID& peer_id);

  static inline std::atomic<std::uint64_t> nextSystemMessageId{10000000000000000000ULL};
  std::shared_ptr<Client> client_;

  mutable std::mutex state_mutex_;

  // --- Decomposed State Members ---
  UIState::MyProfile my_profile_;
  ServiceConnectionState connection_state_ = ServiceConnectionState::unknown;
  std::vector<UIState::ViewContact> contacts_;
  std::optional<ContactID> selected_contact_id_;
  std::deque<messaging::HistoryEntryRecord> interaction_history_;
  bool has_older_messages_ = false;
  UIState::CallState call_state_;
  std::optional<std::string> signaling_server_url_;

  std::unordered_map<ContactID, size_t> chat_pages_loaded_;
  DispatchQueue dispatch_queue_{"ViewModel", 1};

  static constexpr size_t kChatPageSize = 50;

  std::function<void(const std::vector<StateChange>&)> on_state_changed_callback_;
};

inline MercuryViewModel::MercuryViewModel(std::shared_ptr<Client> client) : client_(std::move(client)) {
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    my_profile_.identity_exists = client_->IdentityExists();
    if (my_profile_.identity_exists) {
      my_profile_.my_id = client_->GetId();
      UpdateMyProfileFromSettings(false);
      signaling_server_url_ = client_->GetSettingsManager()->GetSignalingServerUrl();
      UpdateContacts(false);
    }
  }
  if (my_profile_.identity_exists) {
    UpdateChats(false);
  }
  SetupClientCallbacks();
}


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

inline void MercuryViewModel::SelectContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id]() -> void {
    bool selection_changed{false};
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      selection_changed = selected_contact_id_ != peer_id;
      if (selection_changed) {
        selected_contact_id_ = peer_id;
        LoadInitialChatHistory(peer_id);
      }
    }
    if (selection_changed) {
      NotifyStateChanged({StateChange::SelectedContact, StateChange::InteractionHistory});
    }
  });
}

inline void MercuryViewModel::DeselectContact() {
  dispatch_queue_.Dispatch([this] {
    bool changed = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (selected_contact_id_.has_value()) {
        selected_contact_id_.reset();
        interaction_history_.clear();
        has_older_messages_ = false;
        changed = true;
      }
    }
    if (changed) {
      NotifyStateChanged({StateChange::SelectedContact, StateChange::InteractionHistory});
    }
  });
}

inline void MercuryViewModel::SendMessage(const std::string& content) {
  dispatch_queue_.Dispatch([this, content] {
    std::optional<ContactID> selected_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      selected_id = selected_contact_id_;
      if (!selected_id) {
        LOGW << "SendMessage called with no selected contact.";
        return;
      }
    }

    auto entry = client_->SendChatMessage(*selected_id, content);

    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (selected_contact_id_ == selected_id) {
        interaction_history_.push_back(entry);
      }
    }

    UpdateChats(true);
    NotifyStateChanged({StateChange::InteractionHistory});
  });
}

inline void MercuryViewModel::StartVoiceCall() {
  dispatch_queue_.Dispatch([this] {
    std::optional<ContactID> peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      peer_id = selected_contact_id_;
    }
    if (peer_id) {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        call_state_ = {.is_connecting = true, .is_in_call = false, .is_ringing = false, .call_direction = messaging::CallDirection::outgoing, .peer_id = *peer_id, .call_type = messaging::HistoryEntryKind::voiceCall};
      }
      client_->StartVoiceCall(*peer_id);
      UpdateChats(true);
      NotifyStateChanged({StateChange::CallState});
    }
  });
}

inline void MercuryViewModel::StartVideoCall() {
  dispatch_queue_.Dispatch([this] {
    std::optional<ContactID> peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      peer_id = selected_contact_id_;
    }
    if (peer_id) {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        call_state_ = {.is_connecting = true, .is_in_call = false, .is_ringing = false, .call_direction = messaging::CallDirection::outgoing, .peer_id = *peer_id, .call_type = messaging::HistoryEntryKind::videoCall};
      }
      client_->StartVideoCall(*peer_id);
      UpdateChats(true);
      NotifyStateChanged({StateChange::CallState});
    }
  });
}

inline void MercuryViewModel::Hangup() {
  dispatch_queue_.Dispatch([this] {
    ContactID peer_id_to_hangup;
    bool changed = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!call_state_.is_connecting && !call_state_.is_in_call && !call_state_.is_ringing) { return; }
      peer_id_to_hangup = call_state_.peer_id;
      call_state_ = {};
      changed = true;
    }
    if (!peer_id_to_hangup.IsEmpty()) { client_->Hangup(peer_id_to_hangup); }
    if (changed) { NotifyStateChanged({StateChange::CallState}); }
  });
}

inline void MercuryViewModel::AcceptCall() {
  dispatch_queue_.Dispatch([this] {
    ContactID peer_id;
    messaging::HistoryEntryKind call_kind;
    bool changed = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!call_state_.is_ringing) { return; }
      peer_id = call_state_.peer_id;
      call_kind = call_state_.call_type;
      call_state_.is_connecting = false;
      call_state_.is_in_call = true;
      call_state_.is_ringing = false;
      changed = true;
    }
    if (!peer_id.IsEmpty()) { client_->AcceptCall(peer_id, call_kind); }
    if (changed) { NotifyStateChanged({StateChange::CallState}); }
  });
}

inline void MercuryViewModel::RejectCall() {
  dispatch_queue_.Dispatch([this] {
    ContactID peer_id;
    bool changed = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!call_state_.is_ringing) { return; }
      peer_id = call_state_.peer_id;
      call_state_ = {};
      changed = true;
    }
    if (!peer_id.IsEmpty()) { client_->RejectCall(peer_id); }
    if (changed) { NotifyStateChanged({StateChange::CallState}); }
  });
}

inline void MercuryViewModel::AddContact(const ContactID& peer_id, const std::optional<std::string>& token) {
  dispatch_queue_.Dispatch([this, peer_id, token] { client_->AddContact(peer_id, token); });
}

inline auto MercuryViewModel::GenerateContactToken() -> std::string {
  return client_->GenerateContactToken();
}

inline void MercuryViewModel::UpdateSignalingUrl(const std::string& url) {
  dispatch_queue_.Dispatch([this, url] { client_->UpdateSignalingUrl(url); });
}

inline void MercuryViewModel::BlockContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->BlockContact(peer_id);
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (selected_contact_id_ && *selected_contact_id_ == peer_id) {
        selected_contact_id_.reset();
        interaction_history_.clear();
        has_older_messages_ = false;
      }
      UpdateContacts(false);
    }
    UpdateChats(false);
    NotifyStateChanged({StateChange::Contacts, StateChange::SelectedContact, StateChange::InteractionHistory});
  });
}

inline void MercuryViewModel::DeleteContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->DeleteContact(peer_id);
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (selected_contact_id_ && *selected_contact_id_ == peer_id) {
        selected_contact_id_.reset();
        interaction_history_.clear();
        has_older_messages_ = false;
      }
      UpdateContacts(false);
    }
    UpdateChats(false);
    NotifyStateChanged({StateChange::Contacts, StateChange::SelectedContact, StateChange::InteractionHistory});
  });
}

inline void MercuryViewModel::AcceptContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] { client_->ConfirmContact(peer_id); });
}

inline void MercuryViewModel::UnblockContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->UnblockContact(peer_id);
  });
}

inline void MercuryViewModel::UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) {
  dispatch_queue_.Dispatch([this, name, bio, photo] { client_->UpdateMyProfile(name, bio, photo); });
}

inline void MercuryViewModel::SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& name) {
  dispatch_queue_.Dispatch([this, peer_id, name] {
    client_->SetContactDisplayNameOverride(peer_id, name);
    RefreshState();
  });
}

inline void MercuryViewModel::ResetContactDisplayNameOverride(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->ResetContactDisplayNameOverride(peer_id);
    RefreshState();
  });
}

inline void MercuryViewModel::LoadOlderMessages() {
  dispatch_queue_.Dispatch([this] {
    bool changed = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!selected_contact_id_) { return; }

      const auto& peer_id = *selected_contact_id_;
      const size_t pages_loaded = chat_pages_loaded_[peer_id];

      auto page = client_->GetMessageHistoryPage(peer_id, pages_loaded, kChatPageSize);
      if (!page.messages.empty()) {
        for (auto it = page.messages.rbegin(); it != page.messages.rend(); ++it) {
          interaction_history_.push_front(*it);
        }
        chat_pages_loaded_[peer_id]++;
        has_older_messages_ = (chat_pages_loaded_[peer_id] * kChatPageSize) < page.total_messages;
        changed = true;
      } else if (has_older_messages_) {
        has_older_messages_ = false;
        changed = true;
      }
    }
    if (changed) { NotifyStateChanged({StateChange::InteractionHistory}); }
  });
}

inline void MercuryViewModel::LoadFullMessage(uint64_t message_id) {
  dispatch_queue_.Dispatch([this, message_id] {
    auto full_content = client_->GetFullMessageContent(message_id);
    if (!full_content) {
      LOGW << "Could not load full content for message ID: " << message_id;
      return;
    }

    bool updated = false;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (selected_contact_id_) {
        for (auto& entry: interaction_history_) {
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

    if (updated) { NotifyStateChanged({StateChange::InteractionHistory}); }
  });
}

inline void MercuryViewModel::RefreshState() {
  dispatch_queue_.Dispatch([this] {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      my_profile_.identity_exists = client_->IdentityExists();
      my_profile_.my_id = client_->GetId();
      UpdateMyProfileFromSettings(false);
      signaling_server_url_ = client_->GetSettingsManager()->GetSignalingServerUrl();
      UpdateContacts(false);
    }
    UpdateChats(false);
    NotifyStateChanged({StateChange::MyProfile, StateChange::Contacts, StateChange::SignalingUrl});
  });
}

inline void MercuryViewModel::MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids) {
  dispatch_queue_.Dispatch([this, message_ids] {
    std::optional<ContactID> peer_id;
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      peer_id = selected_contact_id_;
    }
    if (peer_id && !message_ids.empty()) {
      client_->MarkMessagesAsRead(*peer_id, message_ids);
      UpdateChats(true);
    }
  });
}

inline void MercuryViewModel::AddSystemMessageToChat(const std::string& message) {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  interaction_history_.push_back({.id = nextSystemMessageId++, .kind = messaging::HistoryEntryKind::systemMessage, .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()), .direction = messaging::MessageDirection::received, .is_truncated = false, .data = messaging::MessageEntry{message}});
  NotifyStateChanged({StateChange::InteractionHistory});
}

inline void MercuryViewModel::SelectContactByIndex(size_t index) {
  ContactID new_selection_id;
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    if (index < contacts_.size()) { new_selection_id = contacts_[index].peer_id; }
  }
  if (!new_selection_id.IsEmpty()) {
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
  return {
      .my_profile = my_profile_,
      .connection_state = connection_state_,
      .contacts = contacts_,
      .selected_contact_id = selected_contact_id_,
      .interaction_history = interaction_history_,
      .has_older_messages = has_older_messages_,
      .call_state = call_state_,
      .signaling_server_url = signaling_server_url_};
}

inline auto MercuryViewModel::GetMyProfile() const -> UIState::MyProfile {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return my_profile_;
}

inline auto MercuryViewModel::GetContacts() const -> std::vector<UIState::ViewContact> {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return contacts_;
}

inline auto MercuryViewModel::GetInteractionHistory() const -> std::deque<messaging::HistoryEntryRecord> {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return interaction_history_;
}

inline auto MercuryViewModel::HasOlderMessages() const -> bool {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return has_older_messages_;
}

inline auto MercuryViewModel::GetCallState() const -> UIState::CallState {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return call_state_;
}


inline void MercuryViewModel::SetOnStateChangedCallback(std::function<void(const std::vector<StateChange>&)> callback) { on_state_changed_callback_ = std::move(callback); }

inline void MercuryViewModel::LoadInitialChatHistory(const ContactID& peer_id) {
  auto page = client_->GetMessageHistoryPage(peer_id, 0, kChatPageSize);
  interaction_history_.assign(page.messages.begin(), page.messages.end());
  chat_pages_loaded_[peer_id] = 1;
  has_older_messages_ = (kChatPageSize < page.total_messages);
}

inline void MercuryViewModel::SetupClientCallbacks() {
  client_->SetChatMessageReceivedCallback([this](const ContactID& peer_id,
                                                 const messaging::HistoryEntryRecord& message) {
    dispatch_queue_.Dispatch([this, peer_id, message] {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (selected_contact_id_ && *selected_contact_id_ == peer_id) {
          interaction_history_.push_back(message);
        }
      }
      UpdateChats(true);
      NotifyStateChanged({StateChange::InteractionHistory});
    });
  });

  client_->SetContactRequestCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        UpdateContacts(false);
        if (selected_contact_id_ &&
            *selected_contact_id_ == peer_id) {
          AddSystemMessageToChat("--> Contact request received. Use '/accept' to confirm.");
        }
      }
      NotifyStateChanged({StateChange::Contacts});
    });
  });

  client_->SetOnContactProfileUpdatedCallback([this](const ContactID& /*peer_id*/) {
    dispatch_queue_.Dispatch([this] {
      UpdateContacts(true);
    });
  });

  client_->SetOnMyProfileUpdatedCallback([this] {
    dispatch_queue_.Dispatch([this] {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        UpdateMyProfileFromSettings(false);
      }
      NotifyStateChanged({StateChange::MyProfile});
    });
  });

  client_->SetContactStatusChangedCallback(
      [this](const ContactID& /*peer_id*/, identity::ContactStatus /*status*/) { RefreshState(); });

  client_->SetStatusCallback([this](const ContactID& peer_id, EventSource source, int state_val) {
    dispatch_queue_.Dispatch([this, peer_id, source, state_val] {
      bool state_changed = false;
      StateChange change_type = StateChange::Connection;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (source == EventSource::system) {
          auto new_state = static_cast<ServiceConnectionState>(state_val);
          if (connection_state_ != new_state) {
            connection_state_ = new_state;
            if (connection_state_ == ServiceConnectionState::online) {
              signaling_server_url_ = client_->GetSettingsManager()->GetSignalingServerUrl();
              NotifyStateChanged({StateChange::SignalingUrl});
            }
            state_changed = true;
            change_type = StateChange::Connection;
          }
        } else if (source == EventSource::peerConnectionState) {
          const auto state = static_cast<eillance::transport::PeerConnectionState>(state_val);
          for (auto& contact: contacts_) {
            if (contact.peer_id == peer_id) {
              if (contact.connection_state != state) {
                contact.connection_state = state;
                state_changed = true;
                change_type = StateChange::Contacts;
              }
              break;
            }
          }
          if (state == transport::PeerConnectionState::Closed ||
              state == transport::PeerConnectionState::Disconnected ||
              state == transport::PeerConnectionState::Failed) {
            if ((call_state_.is_connecting || call_state_.is_in_call || call_state_.is_ringing) && call_state_.peer_id == peer_id) {
              call_state_ = {};
              NotifyStateChanged({StateChange::CallState});
            }
          }
        }
      }
      if (state_changed) { NotifyStateChanged({change_type}); }
    });
  });

  client_->SetIncomingCallCallback([this](const ContactID& peer_id, messaging::HistoryEntryKind call_type) {
    dispatch_queue_.Dispatch([this, peer_id, call_type] {
      bool state_changed = false;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (!call_state_.is_connecting && !call_state_.is_in_call && !call_state_.is_ringing) {
          call_state_.is_connecting = false;
          call_state_.is_ringing = true;
          call_state_.peer_id = peer_id;
          call_state_.call_type = call_type;
          call_state_.call_direction = messaging::CallDirection::incoming;
          state_changed = true;
        }
      }
      if (state_changed) { NotifyStateChanged({StateChange::CallState}); }
    });
  });

  client_->SetOnCallTerminatedCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      bool state_changed = false;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if ((call_state_.is_connecting || call_state_.is_in_call || call_state_.is_ringing) && call_state_.peer_id == peer_id) {
          call_state_ = {};
          state_changed = true;
        }
      }
      if (state_changed) { NotifyStateChanged({StateChange::CallState}); }
    });
  });

  client_->SetOnCallRingingCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      bool state_changed = false;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (call_state_.is_connecting && call_state_.peer_id == peer_id) {
          call_state_.is_connecting = false;
          call_state_.is_ringing = true;
          state_changed = true;
        }
      }
      if (state_changed) {
        NotifyStateChanged({StateChange::CallState});
      }
    });
  });

  client_->SetOnCallAcceptedCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      bool state_changed = false;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        // This is for the caller: transition from ringing to in-call.
        if ((call_state_.is_connecting || call_state_.is_ringing) &&
            call_state_.peer_id == peer_id &&
            call_state_.call_direction == messaging::CallDirection::outgoing) {
          call_state_.is_connecting = false;
          call_state_.is_ringing = false;
          call_state_.is_in_call = true;
          state_changed = true;
        }
      }
      if (state_changed) {
        NotifyStateChanged({StateChange::CallState});
      }
    });
  });
  client_->SetOnContactRemovedCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        if (selected_contact_id_ && *selected_contact_id_ == peer_id) {
          selected_contact_id_.reset();
          interaction_history_.clear();
          has_older_messages_ = false;
        }
      }
      RefreshState();
    });
  });


  client_->SetMessageStatusChangedCallback(
      [this](const ContactID& peer_id, const std::uint64_t message_id,
             const messaging::MessageStatus status) {
        dispatch_queue_.Dispatch([this, peer_id, message_id, status] {
          bool changed = false;
          {
            const std::lock_guard<std::mutex> lock(state_mutex_);
            if (selected_contact_id_ &&
                *selected_contact_id_ == peer_id) {
              for (auto& entry: interaction_history_) {
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
          if (changed) { NotifyStateChanged({StateChange::InteractionHistory}); }
        });
      });
}

inline void MercuryViewModel::NotifyStateChanged(const std::vector<StateChange>& changes) const {
  if (on_state_changed_callback_) { on_state_changed_callback_(changes); }
}

inline void MercuryViewModel::UpdateContacts(bool notify) {
  auto contacts_from_manager = client_->GetContacts();
  contacts_.clear();
  for (const auto& c: contacts_from_manager) {
    const std::string display_name = c.display_name_override.value_or(c.display_name.value_or(c.peer_id));
    contacts_.push_back({.peer_id = ContactID(c.peer_id), .display_name = display_name, .bio = c.bio, .photo = c.photo_base64, .status = c.status, .connection_state = client_->GetPeerConnectionState(ContactID(c.peer_id)), .unread_count = 0});
  }
  if (notify) { NotifyStateChanged({StateChange::Contacts}); }
}

inline void MercuryViewModel::UpdateMyProfileFromSettings(bool notify) {
  const auto* settings = client_->GetSettingsManager();
  my_profile_.my_display_name = settings->GetMyDisplayName();
  my_profile_.my_bio = settings->GetMyBio();
  my_profile_.my_photo = settings->GetMyPhotoBase64();
  if (notify) { NotifyStateChanged({StateChange::MyProfile}); }
}

inline void MercuryViewModel::UpdateChats(bool notify) {
  std::vector<std::string> contact_ids;
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    contact_ids.reserve(contacts_.size());
    for (const auto& contact: contacts_) {
      contact_ids.push_back(contact.peer_id.ToString());
    }
  }

  auto new_chats = client_->GetChatSummaries(contact_ids);

  {
    const std::lock_guard<std::mutex> lock(state_mutex_);

    std::unordered_map<ContactID, uint32_t> unread_counts;
    for (const auto& chat: new_chats) {
      unread_counts[ContactID(chat.peer_id)] = chat.unread_count;
    }

    for (auto& contact: contacts_) {
      if (unread_counts.contains(contact.peer_id)) {
        contact.unread_count = unread_counts.at(contact.peer_id);
      } else {
        contact.unread_count = 0;
      }
    }
  }
  if (notify) {
    NotifyStateChanged({StateChange::Contacts});
  }
}

}  // namespace eillance::view_model
