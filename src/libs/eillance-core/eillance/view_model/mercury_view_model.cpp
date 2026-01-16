#include "eillance/view_model/mercury_view_model.hpp"

#include <algorithm>
#include <variant>

namespace eillance::view_model {

MercuryViewModel::MercuryViewModel(std::shared_ptr<Client> client) : client_(std::move(client)) {
  {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    my_profile_.identity_exists = client_->IdentityExists();
    if (my_profile_.identity_exists) {
      my_profile_.my_id = client_->GetId();
      UpdateMyProfileFromSettings(false);
      signaling_server_url_ = client_->GetSettingsManager()->GetSignalingServerUrl();
      peer_inactivity_timeout_seconds_ = static_cast<int>(client_->GetSettingsManager()->GetPeerInactivityTimeout().count());
      UpdateContacts(false);
    }
  }
  if (my_profile_.identity_exists) {
    UpdateChats(false);
  }
  SetupClientCallbacks();
}

auto MercuryViewModel::GenerateNewIdentity() -> std::string {
  auto mnemonic = client_->GenerateNewIdentity();
  if (!mnemonic.empty()) {
    dispatch_queue_.Dispatch([this] {
      RefreshState();
    });
  }
  return mnemonic;
}

auto MercuryViewModel::RestoreIdentity(const std::string& mnemonic) -> bool {
  if (client_->RestoreIdentity(mnemonic)) {
    dispatch_queue_.Dispatch([this] {
      RefreshState();
    });
    return true;
  }
  return false;
}

auto MercuryViewModel::GetMnemonic() -> std::optional<std::string> {
  return client_->GetMnemonic();
}

auto MercuryViewModel::CreateBackup(const std::string& path, const std::string& password) -> bool {
  return client_->CreateBackup(path, password);
}

auto MercuryViewModel::RestoreBackup(const std::string& path, const std::string& password) -> bool {
  return client_->RestoreBackup(path, password);
}

void MercuryViewModel::SelectContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id]() -> void {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (selected_contact_id_ != peer_id) {
        selected_contact_id_ = peer_id;
        LoadInitialChatHistory(peer_id);
      }
    }
    NotifyStateChanged({StateChange::SelectedContact, StateChange::InteractionHistory});
  });
}

void MercuryViewModel::DeselectContact() {
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

void MercuryViewModel::SendMessage(const std::string& content) {
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

void MercuryViewModel::StartVoiceCall() {
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

void MercuryViewModel::StartVideoCall() {
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

void MercuryViewModel::Hangup() {
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

void MercuryViewModel::AcceptCall() {
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

void MercuryViewModel::RejectCall() {
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

void MercuryViewModel::AddContact(const ContactID& peer_id, const std::optional<std::string>& token) {
  dispatch_queue_.Dispatch([this, peer_id, token] { client_->AddContact(peer_id, token); });
}

auto MercuryViewModel::GenerateContactToken() -> std::string {
  return client_->GenerateContactToken();
}

void MercuryViewModel::UpdateSignalingUrl(const std::string& url) {
  dispatch_queue_.Dispatch([this, url] { client_->UpdateSignalingUrl(url); });
}

void MercuryViewModel::SetPeerInactivityTimeout(int seconds) {
  dispatch_queue_.Dispatch([this, seconds] {
    client_->GetSettingsManager()->SetPeerInactivityTimeout(std::chrono::seconds(seconds));
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      peer_inactivity_timeout_seconds_ = seconds;
    }
    NotifyStateChanged({StateChange::PeerInactivityTimeout});
  });
}

void MercuryViewModel::BlockContact(const ContactID& peer_id) {
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

void MercuryViewModel::DeleteContact(const ContactID& peer_id) {
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

void MercuryViewModel::AcceptContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] { client_->ConfirmContact(peer_id); });
}

void MercuryViewModel::UnblockContact(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->UnblockContact(peer_id);
  });
}

void MercuryViewModel::UpdateMyProfile(const std::optional<std::string>& name, const std::optional<std::string>& bio, const std::optional<std::string>& photo) {
  dispatch_queue_.Dispatch([this, name, bio, photo] { client_->UpdateMyProfile(name, bio, photo); });
}

void MercuryViewModel::SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& name) {
  dispatch_queue_.Dispatch([this, peer_id, name] {
    client_->SetContactDisplayNameOverride(peer_id, name);
    RefreshState();
  });
}

void MercuryViewModel::ResetContactDisplayNameOverride(const ContactID& peer_id) {
  dispatch_queue_.Dispatch([this, peer_id] {
    client_->ResetContactDisplayNameOverride(peer_id);
    RefreshState();
  });
}

void MercuryViewModel::LoadOlderMessages() {
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

void MercuryViewModel::LoadFullMessage(uint64_t message_id) {
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

void MercuryViewModel::RefreshState() {
  dispatch_queue_.Dispatch([this] {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      my_profile_.identity_exists = client_->IdentityExists();
      my_profile_.my_id = client_->GetId();
      UpdateMyProfileFromSettings(false);
      signaling_server_url_ = client_->GetSettingsManager()->GetSignalingServerUrl();
      peer_inactivity_timeout_seconds_ = static_cast<int>(client_->GetSettingsManager()->GetPeerInactivityTimeout().count());
      UpdateContacts(false);
    }
    UpdateChats(false);
    NotifyStateChanged({StateChange::MyProfile, StateChange::Contacts, StateChange::SignalingUrl, StateChange::Connection, StateChange::CallState, StateChange::SelectedContact, StateChange::InteractionHistory, StateChange::PeerInactivityTimeout});
  });
}

void MercuryViewModel::MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids) {
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

void MercuryViewModel::AddSystemMessageToChat(const std::string& message) {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  interaction_history_.push_back({.id = nextSystemMessageId++, .kind = messaging::HistoryEntryKind::systemMessage, .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()), .direction = messaging::MessageDirection::received, .is_truncated = false, .data = messaging::MessageEntry{message}});
  NotifyStateChanged({StateChange::InteractionHistory});
}

void MercuryViewModel::SelectContactByIndex(size_t index) {
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

void MercuryViewModel::DebugEraseAllHistory() {
  dispatch_queue_.Dispatch([this] {
    client_->DebugEraseAllHistory();
    RefreshState();
  });
}

void MercuryViewModel::DebugEraseAllContacts() {
  dispatch_queue_.Dispatch([this] {
    client_->DebugEraseAllContacts();
    RefreshState();
  });
}

void MercuryViewModel::DebugRevokeAllApprovals() {
  dispatch_queue_.Dispatch([this] {
    client_->DebugRevokeAllApprovals();
    RefreshState();
  });
}

auto MercuryViewModel::GetState() const -> UIState {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return {
      .my_profile = my_profile_,
      .connection_state = connection_state_,
      .contacts = contacts_,
      .selected_contact_id = selected_contact_id_,
      .interaction_history = interaction_history_,
      .has_older_messages = has_older_messages_,
      .call_state = call_state_,
      .signaling_server_url = signaling_server_url_,
      .peer_inactivity_timeout_seconds = peer_inactivity_timeout_seconds_};
}

auto MercuryViewModel::GetMyProfile() const -> UIState::MyProfile {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return my_profile_;
}

auto MercuryViewModel::GetContacts() const -> std::vector<UIState::ViewContact> {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return contacts_;
}

auto MercuryViewModel::GetInteractionHistory() const -> std::deque<messaging::HistoryEntryRecord> {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return interaction_history_;
}

auto MercuryViewModel::HasOlderMessages() const -> bool {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return has_older_messages_;
}

auto MercuryViewModel::GetCallState() const -> UIState::CallState {
  const std::lock_guard<std::mutex> lock(state_mutex_);
  return call_state_;
}

void MercuryViewModel::SetOnStateChangedCallback(std::function<void(const std::vector<StateChange>&)> callback) { on_state_changed_callback_ = std::move(callback); }

void MercuryViewModel::LoadInitialChatHistory(const ContactID& peer_id) {
  auto page = client_->GetMessageHistoryPage(peer_id, 0, kChatPageSize);
  interaction_history_.assign(page.messages.begin(), page.messages.end());
  chat_pages_loaded_[peer_id] = 1;
  has_older_messages_ = (kChatPageSize < page.total_messages);
}

void MercuryViewModel::SetupClientCallbacks() {
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

void MercuryViewModel::NotifyStateChanged(const std::vector<StateChange>& changes) const {
  if (on_state_changed_callback_) { on_state_changed_callback_(changes); }
}

void MercuryViewModel::UpdateContacts(bool notify) {
  auto contacts_from_manager = client_->GetContacts();
  contacts_.clear();
  for (const auto& c: contacts_from_manager) {
    std::string display_name;
    if (c.display_name_override) {
      display_name = *c.display_name_override;
    } else if (c.display_name) {
      display_name = *c.display_name;
    }

    contacts_.push_back({.peer_id = ContactID(c.peer_id), .display_name = display_name, .bio = c.bio, .photo = c.photo_base64, .status = c.status, .connection_state = client_->GetPeerConnectionState(ContactID(c.peer_id)), .unread_count = 0});
  }
  if (notify) { NotifyStateChanged({StateChange::Contacts}); }
}

void MercuryViewModel::UpdateMyProfileFromSettings(bool notify) {
  const auto* settings = client_->GetSettingsManager();
  my_profile_.my_display_name = settings->GetMyDisplayName();
  my_profile_.my_bio = settings->GetMyBio();
  my_profile_.my_photo = settings->GetMyPhotoBase64();
  if (notify) { NotifyStateChanged({StateChange::MyProfile}); }
}

void MercuryViewModel::UpdateChats(bool notify) {
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

    std::unordered_map<ContactID, messaging::ChatSummary> chat_map;
    for (const auto& chat: new_chats) {
      chat_map[ContactID(chat.peer_id)] = chat;
    }

    for (auto& contact: contacts_) {
      if (chat_map.contains(contact.peer_id)) {
        const auto& summary = chat_map.at(contact.peer_id);
        contact.unread_count = summary.unread_count;
        contact.last_activity_timestamp = summary.last_message.timestamp;

        // Populate snippet
        if (std::holds_alternative<messaging::MessageEntry>(summary.last_message.data)) {
          const auto& content = std::get<messaging::MessageEntry>(summary.last_message.data).content;
          contact.last_message_snippet = (content.length() > 100) ? content.substr(0, 100) + "..." : content;
        } else if (std::holds_alternative<messaging::CallEntry>(summary.last_message.data)) {
          if (summary.last_message.kind == messaging::HistoryEntryKind::videoCall) {
            contact.last_message_snippet = "Video Call";
          } else {
            contact.last_message_snippet = "Voice Call";
          }
        }
      } else {
        contact.unread_count = 0;
        contact.last_activity_timestamp = 0;
        contact.last_message_snippet.reset();
      }
    }

    std::sort(contacts_.begin(), contacts_.end(), [](const auto& a, const auto& b) {
      return a.last_activity_timestamp > b.last_activity_timestamp;
    });
  }
  if (notify) {
    NotifyStateChanged({StateChange::Contacts});
  }
}

}  // namespace eillance::view_model
