#include "contacts_view_model.hpp"

#include <algorithm>
#include <unordered_map>

#include "eillance/logging/logging.h"

namespace eillance::view_model {

ContactsViewModel::ContactsViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier)
    : client_(std::move(client)), notifier_(std::move(notifier)) {
}

auto ContactsViewModel::GetContacts() const -> std::vector<ViewContact> {
  const std::lock_guard<std::mutex> lock(mutex_);
  return contacts_;
}

void ContactsViewModel::AddContact(const ContactID& peer_id, const std::optional<std::string>& token) {
  client_->AddContact(peer_id, token);
  // Client callback will trigger refresh, but we can optimistically update or wait.
  // The original implementation triggered via callback.
}

void ContactsViewModel::BlockContact(const ContactID& peer_id) {
  client_->BlockContact(peer_id);
  UpdateContactsList();
}

void ContactsViewModel::DeleteContact(const ContactID& peer_id) {
  client_->DeleteContact(peer_id);
  UpdateContactsList();
}

void ContactsViewModel::UnblockContact(const ContactID& peer_id) {
  client_->UnblockContact(peer_id);
  // Unblocking triggers a status change callback in client, which will refresh.
}

void ContactsViewModel::AcceptContact(const ContactID& peer_id) {
  client_->ConfirmContact(peer_id);
}

void ContactsViewModel::SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& name) {
  client_->SetContactDisplayNameOverride(peer_id, name);
  UpdateContactsList();
}

void ContactsViewModel::ResetContactDisplayNameOverride(const ContactID& peer_id) {
  client_->ResetContactDisplayNameOverride(peer_id);
  UpdateContactsList();
}

auto ContactsViewModel::GenerateContactToken() -> std::string {
  return client_->GenerateContactToken();
}

void ContactsViewModel::UpdateContactsList(bool notify) {
  const std::lock_guard<std::mutex> lock(mutex_);

  auto contacts_from_manager = client_->GetContacts();

  // 1. Convert to ViewContact
  std::vector<ViewContact> temp_contacts;
  temp_contacts.reserve(contacts_from_manager.size());
  std::vector<std::string> contact_ids;
  contact_ids.reserve(contacts_from_manager.size());

  for (const auto& c: contacts_from_manager) {
    std::string display_name;
    if (c.display_name_override) {
      display_name = *c.display_name_override;
    } else if (c.display_name) {
      display_name = *c.display_name;
    }

    temp_contacts.push_back({.peer_id = ContactID(c.peer_id),
                             .display_name = display_name,
                             .bio = c.bio,
                             .photo = c.photo_base64,
                             .status = c.status,
                             .connection_state = client_->GetPeerConnectionState(ContactID(c.peer_id)),
                             .unread_count = 0});
    contact_ids.push_back(c.peer_id);
  }

  // 2. Fetch Chat Summaries to populate snippets and sort
  auto chat_summaries = client_->GetChatSummaries(contact_ids);
  std::unordered_map<ContactID, messaging::ChatSummary> chat_map;
  for (const auto& chat: chat_summaries) {
    chat_map[ContactID(chat.peer_id)] = chat;
  }

  for (auto& contact: temp_contacts) {
    if (chat_map.contains(contact.peer_id)) {
      const auto& summary = chat_map.at(contact.peer_id);
      contact.unread_count = summary.unread_count;
      contact.last_activity_timestamp = summary.last_message.timestamp;

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

  // 3. Sort by activity
  std::sort(temp_contacts.begin(), temp_contacts.end(), [](const auto& a, const auto& b) {
    return a.last_activity_timestamp > b.last_activity_timestamp;
  });

  contacts_ = std::move(temp_contacts);

  if (notify && notifier_) {
    notifier_(StateChange::Contacts);
  }
}

void ContactsViewModel::UpdateContactConnectionState(const ContactID& peer_id, transport::PeerConnectionState state) {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto& contact: contacts_) {
      if (contact.peer_id == peer_id) {
        if (contact.connection_state != state) {
          contact.connection_state = state;
          changed = true;
        }
        break;
      }
    }
  }
  if (changed && notifier_) {
    notifier_(StateChange::Contacts);
  }
}

void ContactsViewModel::DebugEraseAllContacts() {
  client_->DebugEraseAllContacts();
  UpdateContactsList();
}

void ContactsViewModel::DebugRevokeAllApprovals() {
  client_->DebugRevokeAllApprovals();
  UpdateContactsList();
}

}  // namespace eillance::view_model
