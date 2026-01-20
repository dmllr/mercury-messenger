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
      : client_(std::move(client)),
        router_(std::move(router)) {}

  void SetObserver(std::shared_ptr<Observer> observer) {
    std::shared_ptr<Observer> local_obs;
    std::vector<ViewContact> current_contacts;
    {
      std::lock_guard<std::mutex> const lock(mutex_);
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
    std::lock_guard<std::mutex> const lock(mutex_);
    return contacts_;
  }

  // --- Actions (run asynchronously to avoid blocking main thread) ---

  void AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) {
    std::thread([self = shared_from_this(), peer_id, token]() { self->client_->AddContact(peer_id, token); }).detach();
  }

  void BlockContact(ContactID const& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->BlockContact(peer_id, true);
      self->DoUpdateContactsList();
    }).detach();
  }

  void DeleteContact(ContactID const& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->DeleteContact(peer_id);
      self->DoUpdateContactsList();
    }).detach();
  }

  void UnblockContact(ContactID const& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() { self->client_->UnblockContact(peer_id); }).detach();
  }

  void AcceptContact(ContactID const& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() { self->client_->ConfirmContact(peer_id); }).detach();
  }

  void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& name) {
    std::thread([self = shared_from_this(), peer_id, name]() {
      self->client_->SetContactDisplayNameOverride(peer_id, name);
      self->DoUpdateContactsList();
    }).detach();
  }

  void ResetContactDisplayNameOverride(ContactID const& peer_id) {
    std::thread([self = shared_from_this(), peer_id]() {
      self->client_->ResetContactDisplayNameOverride(peer_id);
      self->DoUpdateContactsList();
    }).detach();
  }

  void UpdateContactsList() {
    std::thread([self = shared_from_this()]() { self->DoUpdateContactsList(); }).detach();
  }

  void DoUpdateContactsList() {
    std::vector<ViewContact> temp_contacts;
    {
      auto const contacts_from_manager = client_->GetContacts();
      temp_contacts.reserve(contacts_from_manager.size());
      std::vector<ContactID> contact_ids;
      contact_ids.reserve(contacts_from_manager.size());

      for (auto const& contact : contacts_from_manager) {
        std::string display_name;
        if (contact.display_name_override) {
          display_name = *contact.display_name_override;
        } else if (contact.display_name) {
          display_name = *contact.display_name;
        }

        temp_contacts.push_back(
            {.peer_id = contact.peer_id,
             .display_name = display_name,
             .bio = contact.bio,
             .photo = contact.photo_base64,
             .status = contact.status,
             .connection_state = client_->GetPeerConnectionState(contact.peer_id),
             .unread_count = 0}
        );
        contact_ids.push_back(contact.peer_id);
      }

      auto const chat_summaries = client_->GetChatSummaries(contact_ids);
      std::unordered_map<ContactID, messaging::ChatSummary> chat_map;
      for (auto const& chat : chat_summaries) {
        chat_map[chat.peer_id] = chat;
      }

      for (auto& contact : temp_contacts) {
        if (chat_map.contains(contact.peer_id)) {
          auto const& summary = chat_map.at(contact.peer_id);
          contact.unread_count = summary.unread_count;
          contact.last_activity_timestamp = summary.last_message.timestamp;

          if (std::holds_alternative<messaging::MessageEntry>(summary.last_message.data)) {
            auto const& content = std::get<messaging::MessageEntry>(summary.last_message.data).content;
            constexpr size_t k_snippet_length = 100;
            contact.last_message_snippet =
                (content.length() > k_snippet_length) ? content.substr(0, k_snippet_length) + "..." : content;
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

      std::ranges::sort(temp_contacts, [](auto const& left, auto const& right) {
        return left.last_activity_timestamp > right.last_activity_timestamp;
      });
    }

    std::shared_ptr<Observer> obs;
    {
      std::lock_guard<std::mutex> const lock(mutex_);
      contacts_ = temp_contacts;  // Keep internal copy
      obs = observer_;
    }
    if (obs) {
      obs->OnContactsUpdated(temp_contacts);
    }
  }

  void UpdateContactConnectionState(ContactID const& peer_id, transport::PeerConnectionState state) {
    std::shared_ptr<Observer> obs;
    std::vector<ViewContact> notify_copy;
    bool changed = false;
    {
      std::lock_guard<std::mutex> const lock(mutex_);
      for (auto& contact : contacts_) {
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

  Subscribe<EventContactRequest>(queue, [this](auto const&) { impl_->UpdateContactsList(); });
  Subscribe<EventContactStatusChanged>(queue, [this](auto const&) { impl_->UpdateContactsList(); });
  Subscribe<EventContactProfileUpdated>(queue, [this](auto const&) { impl_->UpdateContactsList(); });
  Subscribe<EventContactRemoved>(queue, [this](auto const&) { impl_->UpdateContactsList(); });
  Subscribe<EventChatMessageReceived>(queue, [this](auto const&) { impl_->UpdateContactsList(); });
  Subscribe<EventMessageStatusChanged>(queue, [this](auto const&) { impl_->UpdateContactsList(); });
  Subscribe<EventDebugDataReset>(queue, [this](auto const&) { impl_->UpdateContactsList(); });

  Subscribe<EventStatusChanged>(queue, [this](EventStatusChanged const& event) {
    if (event.source == EventSource::peerConnectionState) {
      impl_->UpdateContactConnectionState(event.peer_id, static_cast<transport::PeerConnectionState>(event.state));
    }
  });
}

ContactsViewModel::~ContactsViewModel() {
  for (auto const& unsubscribe : eventUnsubscribers_) {
    unsubscribe();
  }
}

template <typename EventType>
void ContactsViewModel::Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void ContactsViewModel::SetObserver(std::shared_ptr<Observer> observer) const {
  impl_->SetObserver(std::move(observer));
}

auto ContactsViewModel::GetContacts() const -> std::vector<ViewContact> { return impl_->GetContacts(); }

void ContactsViewModel::AddContact(ContactID const& peerId, std::optional<std::vector<std::byte>> const& token) const {
  impl_->AddContact(peerId, token);
}

void ContactsViewModel::BlockContact(ContactID const& peerId) { impl_->BlockContact(peerId); }

void ContactsViewModel::DeleteContact(ContactID const& peerId) { impl_->DeleteContact(peerId); }

void ContactsViewModel::UnblockContact(ContactID const& peerId) { impl_->UnblockContact(peerId); }

void ContactsViewModel::AcceptContact(ContactID const& peerId) { impl_->AcceptContact(peerId); }

void ContactsViewModel::SetContactDisplayNameOverride(ContactID const& peerId, std::string const& name) {
  impl_->SetContactDisplayNameOverride(peerId, name);
}

void ContactsViewModel::ResetContactDisplayNameOverride(ContactID const& peerId) {
  impl_->ResetContactDisplayNameOverride(peerId);
}

auto ContactsViewModel::GenerateContactToken() -> std::vector<std::byte> {
  return impl_->client_->GenerateContactToken();
}

}  // namespace eillance::view_model
