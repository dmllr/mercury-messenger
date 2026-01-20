#include "contacts_view_model.hpp"

#include <algorithm>
#include <thread>
#include <unordered_map>

#include "eillance/logging/logging.hpp"

namespace eillance::view_model {

// --- Implementation Struct ---

struct ContactsViewModel::Impl : public std::enable_shared_from_this<Impl> {
  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  std::vector<ViewContact> contacts_;

  Impl(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : client_(std::move(client)), router_(std::move(router)) {}

  void SetObserver(std::shared_ptr<Observer> observer) {
    std::shared_ptr<Observer> local_obs;
    std::vector<ViewContact> current_contacts;
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      observer_ = std::move(observer);
      local_obs = observer_;
      current_contacts = contacts_;
    }
    // If we have data, notify the new observer immediately outside the lock
    if (local_obs && !current_contacts.empty()) {
      local_obs->OnContactsUpdated(current_contacts);
    }
  }

  [[nodiscard]] auto GetContacts() const -> std::vector<ViewContact> {
    const std::lock_guard<std::mutex> lock(mutex_);
    return contacts_;
  }

  // --- Actions (run asynchronously to avoid blocking main thread) ---

  void AddContact(const ContactID& peer_id, const std::optional<std::vector<std::byte>>& token) {
    std::thread([self = shared_from_this(), peer_id, token]() {
      self->client_->AddContact(peer_id, token);
    }).detach();
  }

  void BlockContact(const ContactID& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->BlockContact(peer_id, true);
      self->DoUpdateContactsList();
    }).detach();
  }

  void DeleteContact(const ContactID& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->DeleteContact(peer_id);
      self->DoUpdateContactsList();
    }).detach();
  }

  void UnblockContact(const ContactID& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->UnblockContact(peer_id);
    }).detach();
  }

  void AcceptContact(const ContactID& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->ConfirmContact(peer_id);
    }).detach();
  }

  void SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& name) {
    std::thread([self = shared_from_this(), peer_id, name]() {
      self->client_->SetContactDisplayNameOverride(peer_id, name);
      self->DoUpdateContactsList();
    }).detach();
  }

  void ResetContactDisplayNameOverride(const ContactID& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->ResetContactDisplayNameOverride(peer_id);
      self->DoUpdateContactsList();
    }).detach();
  }

  void UpdateContactsList() {
    std::thread([self = shared_from_this()]() {
      self->DoUpdateContactsList();
    }).detach();
  }

  void DoUpdateContactsList() {
    std::vector<ViewContact> temp_contacts;
    {
      auto contacts_from_manager = client_->GetContacts();
      temp_contacts.reserve(contacts_from_manager.size());
      std::vector<ContactID> contact_ids;
      contact_ids.reserve(contacts_from_manager.size());

      for (const auto& c: contacts_from_manager) {
        std::string display_name;
        if (c.display_name_override) {
          display_name = *c.display_name_override;
        } else if (c.display_name) {
          display_name = *c.display_name;
        }

        temp_contacts.push_back({.peer_id = c.peer_id,
                                 .display_name = display_name,
                                 .bio = c.bio,
                                 .photo = c.photo_base64,
                                 .status = c.status,
                                 .connection_state = client_->GetPeerConnectionState(c.peer_id),
                                 .unread_count = 0});
        contact_ids.push_back(c.peer_id);
      }

      auto chat_summaries = client_->GetChatSummaries(contact_ids);
      std::unordered_map<ContactID, messaging::ChatSummary> chat_map;
      for (const auto& chat: chat_summaries) {
        chat_map[chat.peer_id] = chat;
      }

      for (auto& contact: temp_contacts) {
        if (chat_map.contains(contact.peer_id)) {
          const auto& summary = chat_map.at(contact.peer_id);
          contact.unread_count = summary.unread_count;
          contact.last_activity_timestamp = summary.last_message.timestamp;

          if (std::holds_alternative<messaging::MessageEntry>(summary.last_message.data)) {
            const auto& content = std::get<messaging::MessageEntry>(summary.last_message.data).content;
            constexpr size_t k_snippet_length = 100;
            contact.last_message_snippet = (content.length() > k_snippet_length) ? content.substr(0, k_snippet_length) + "..." : content;
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

      std::sort(temp_contacts.begin(), temp_contacts.end(), [](const auto& a, const auto& b) {
        return a.last_activity_timestamp > b.last_activity_timestamp;
      });
    }

    std::shared_ptr<Observer> obs;
    {
      const std::lock_guard<std::mutex> lock(mutex_);
      contacts_ = temp_contacts;  // Keep internal copy
      obs = observer_;
    }
    if (obs) {
      obs->OnContactsUpdated(temp_contacts);
    }
  }

  void UpdateContactConnectionState(const ContactID& peer_id, transport::PeerConnectionState state) {
    std::shared_ptr<Observer> obs;
    std::vector<ViewContact> notify_copy;
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
      if (changed) {
        notify_copy = contacts_;
        obs = observer_;
      }
    }
    if (changed && obs) {
      obs->OnContactsUpdated(notify_copy);
    }
  }
};

// --- ContactsViewModel Wrapper ---

ContactsViewModel::ContactsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : impl_(std::make_shared<Impl>(std::move(client), std::move(router))) {

  // Start initial load asynchronously
  impl_->UpdateContactsList();

  auto& queue = impl_->router_->Queue();

  Subscribe<EventContactRequest>(queue, [this](const auto&) { impl_->UpdateContactsList(); });
  Subscribe<EventContactStatusChanged>(queue, [this](const auto&) { impl_->UpdateContactsList(); });
  Subscribe<EventContactProfileUpdated>(queue, [this](const auto&) { impl_->UpdateContactsList(); });
  Subscribe<EventContactRemoved>(queue, [this](const auto&) { impl_->UpdateContactsList(); });
  Subscribe<EventChatMessageReceived>(queue, [this](const auto&) { impl_->UpdateContactsList(); });
  Subscribe<EventMessageStatusChanged>(queue, [this](const auto&) { impl_->UpdateContactsList(); });
  Subscribe<EventDebugDataReset>(queue, [this](const auto&) { impl_->UpdateContactsList(); });

  Subscribe<EventStatusChanged>(queue, [this](const EventStatusChanged& event) {
    if (event.source == EventSource::peerConnectionState) {
      impl_->UpdateContactConnectionState(event.peer_id, static_cast<transport::PeerConnectionState>(event.state));
    }
  });
}

ContactsViewModel::~ContactsViewModel() {
  for (const auto& unsubscribe: eventUnsubscribers_) {
    unsubscribe();
  }
}

template<typename EventType>
void ContactsViewModel::Subscribe(EventQueue& queue, std::function<void(const EventType&)> callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void ContactsViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  impl_->SetObserver(std::move(observer));
}

auto ContactsViewModel::GetContacts() const -> std::vector<ViewContact> {
  return impl_->GetContacts();
}

void ContactsViewModel::AddContact(const ContactID& peerId, const std::optional<std::vector<std::byte>>& token) {
  impl_->AddContact(peerId, token);
}

void ContactsViewModel::BlockContact(const ContactID& peerId) {
  impl_->BlockContact(peerId);
}

void ContactsViewModel::DeleteContact(const ContactID& peerId) {
  impl_->DeleteContact(peerId);
}

void ContactsViewModel::UnblockContact(const ContactID& peerId) {
  impl_->UnblockContact(peerId);
}

void ContactsViewModel::AcceptContact(const ContactID& peerId) {
  impl_->AcceptContact(peerId);
}

void ContactsViewModel::SetContactDisplayNameOverride(const ContactID& peerId, const std::string& name) {
  impl_->SetContactDisplayNameOverride(peerId, name);
}

void ContactsViewModel::ResetContactDisplayNameOverride(const ContactID& peerId) {
  impl_->ResetContactDisplayNameOverride(peerId);
}

auto ContactsViewModel::GenerateContactToken() -> std::vector<std::byte> {
  return impl_->client_->GenerateContactToken();
}

}  // namespace eillance::view_model
