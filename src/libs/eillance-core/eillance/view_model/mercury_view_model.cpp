#include "mercury_view_model.hpp"

#include "eillance/logging/logging.h"

namespace eillance::view_model {

MercuryViewModel::MercuryViewModel(std::shared_ptr<Client> client) : client_(std::move(client)) {
  // Initialize Sub-ViewModels
  auto notifier = [this](StateChange change) { Notify(change); };

  contacts_vm_ = std::make_unique<ContactsViewModel>(client_, notifier);
  conversation_vm_ = std::make_unique<ConversationViewModel>(client_, notifier);
  call_vm_ = std::make_unique<CallViewModel>(client_, notifier);
  profile_vm_ = std::make_unique<ProfileViewModel>(client_, notifier);
  settings_vm_ = std::make_unique<SettingsViewModel>(client_, notifier);

  SetupClientCallbacks();

  // Initial population logic if identity exists
  if (profile_vm_->GetProfile().identity_exists) {
    contacts_vm_->UpdateContactsList(false);
  }
}

MercuryViewModel::~MercuryViewModel() = default;

// --- Global Actions & Routing ---

void MercuryViewModel::RefreshState() {
  dispatch_queue_.Dispatch([this] {
    profile_vm_->RefreshFromSettings();
    settings_vm_->LoadSettings();
    contacts_vm_->UpdateContactsList(false);

    // Legacy compatibility: Refresh triggers a broad notification
    NotifyBatch({StateChange::MyProfile,
                 StateChange::Contacts,
                 StateChange::SignalingUrl,
                 StateChange::Connection,
                 StateChange::CallState,
                 StateChange::SelectedContact,
                 StateChange::InteractionHistory,
                 StateChange::PeerInactivityTimeout});
  });
}

void MercuryViewModel::Notify(StateChange change) {
  // Forward notification from sub-VM thread to main dispatch queue
  dispatch_queue_.Dispatch([this, change] {
    if (on_state_changed_callback_) {
      on_state_changed_callback_({change});
    }
  });
}

void MercuryViewModel::NotifyBatch(const std::vector<StateChange>& changes) {
  if (on_state_changed_callback_) {
    on_state_changed_callback_(changes);
  }
}

void MercuryViewModel::SetOnStateChangedCallback(std::function<void(const std::vector<StateChange>&)> callback) {
  on_state_changed_callback_ = std::move(callback);
}

// --- Legacy State Aggregator ---

auto MercuryViewModel::GetState() const -> UIState {
  // This aggregates state from all sub-VMs.
  // Thread-safety note: Each getter below locks its own mutex.
  // However, the overall UIState is not atomic across all sub-VMs.
  // This is acceptable for UI rendering updates.

  auto profile = profile_vm_->GetProfile();
  auto settings = settings_vm_->GetState();
  auto call = call_vm_->GetState();
  auto conv = conversation_vm_->GetState();
  auto contacts = contacts_vm_->GetContacts();

  return UIState{
      .my_profile = profile,
      .connection_state = settings.connection_state,
      .contacts = contacts,
      .selected_contact_id = conv.selected_contact_id,
      .interaction_history = conv.history,
      .has_older_messages = conv.has_older_messages,
      .call_state = call,
      .signaling_server_url = settings.signaling_server_url,
      .peer_inactivity_timeout_seconds = settings.peer_inactivity_timeout_seconds};
}

// --- Client Callback Wiring ---

void MercuryViewModel::SetupClientCallbacks() {
  // Chat
  client_->SetChatMessageReceivedCallback([this](const ContactID& peer_id, const messaging::HistoryEntryRecord& message) {
    dispatch_queue_.Dispatch([this, peer_id, message] {
      conversation_vm_->OnMessageReceived(peer_id, message);
      contacts_vm_->UpdateContactsList(true);  // Update unread counts/snippets
    });
  });

  client_->SetMessageStatusChangedCallback([this](const ContactID& peer_id, uint64_t message_id, messaging::MessageStatus status) {
    dispatch_queue_.Dispatch([this, peer_id, message_id, status] {
      conversation_vm_->OnMessageStatusChanged(peer_id, message_id, status);
    });
  });

  // Contacts
  client_->SetContactRequestCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      contacts_vm_->UpdateContactsList(true);
      if (auto selected = conversation_vm_->GetSelectedContactId()) {
        if (*selected == peer_id) {
          conversation_vm_->AddSystemMessage("--> Contact request received. Use '/accept' to confirm.");
        }
      }
    });
  });

  client_->SetOnContactProfileUpdatedCallback([this](const ContactID& /*peer_id*/) {
    dispatch_queue_.Dispatch([this] {
      contacts_vm_->UpdateContactsList(true);
    });
  });

  client_->SetContactStatusChangedCallback([this](const ContactID& /*peer_id*/, identity::ContactStatus /*status*/) {
    RefreshState();
  });

  client_->SetOnContactRemovedCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      if (auto selected = conversation_vm_->GetSelectedContactId()) {
        if (*selected == peer_id) {
          conversation_vm_->DeselectContact();
        }
      }
      contacts_vm_->UpdateContactsList(true);
    });
  });

  // Connection & Status
  client_->SetStatusCallback([this](const ContactID& peer_id, EventSource source, int state_val) {
    dispatch_queue_.Dispatch([this, peer_id, source, state_val] {
      if (source == EventSource::system) {
        settings_vm_->OnConnectionStateChanged(static_cast<ServiceConnectionState>(state_val));
      } else if (source == EventSource::peerConnectionState) {
        auto state = static_cast<transport::PeerConnectionState>(state_val);
        contacts_vm_->UpdateContactConnectionState(peer_id, state);

        if (state == transport::PeerConnectionState::Closed ||
            state == transport::PeerConnectionState::Disconnected ||
            state == transport::PeerConnectionState::Failed) {
          call_vm_->OnCallTerminated(peer_id);
        }
      }
    });
  });

  // Calls
  client_->SetIncomingCallCallback([this](const ContactID& peer_id, messaging::HistoryEntryKind call_type) {
    dispatch_queue_.Dispatch([this, peer_id, call_type] {
      call_vm_->OnIncomingCall(peer_id, call_type);
    });
  });

  client_->SetOnCallRingingCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      call_vm_->OnCallRinging(peer_id);
    });
  });

  client_->SetOnCallAcceptedCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      call_vm_->OnCallAccepted(peer_id);
    });
  });

  client_->SetOnCallTerminatedCallback([this](const ContactID& peer_id) {
    dispatch_queue_.Dispatch([this, peer_id] {
      call_vm_->OnCallTerminated(peer_id);
    });
  });

  // Profile
  client_->SetOnMyProfileUpdatedCallback([this] {
    dispatch_queue_.Dispatch([this] {
      profile_vm_->RefreshFromSettings();
    });
  });
}

}  // namespace eillance::view_model
