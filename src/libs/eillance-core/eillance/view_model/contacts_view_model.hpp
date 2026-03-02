#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/utils/event_queue.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ContactsViewModel {
 public:
  explicit ContactsViewModel(std::shared_ptr<Client> client)
      : impl_(std::make_shared<Impl>(std::move(client))) {
    impl_->UpdateContactsList();

    auto& queue = EventQueue::Instance();
    auto impl = impl_;
    Subscribe<EventContactRequest>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventContactStatusChanged>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventContactProfileUpdated>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventContactRemoved>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventChatMessageReceived>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventMessageStatusChanged>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventMessageUpdated>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventDebugDataReset>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventDataReloaded>(queue, [impl](auto const&) { impl->UpdateContactsList(); });

    Subscribe<EventStatusChanged>(queue, [impl](EventStatusChanged const& event) {
      if (event.source == EventSource::peerConnectionState) {
        impl->UpdateContactConnectionState(event.peer_id, static_cast<transport::PeerConnectionState>(event.state));
      }
    });
  }

  ~ContactsViewModel() {
    for (auto const id : eventUnsubscribers_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

  [[nodiscard]] auto GetContacts() const -> std::vector<ViewContact> {
    return impl_->GetContacts();
  }

  [[nodiscard]] auto GetUnreadMessages(ContactID const& peer_id) const -> std::vector<messaging::HistoryEntryRecord> {
    return impl_->client->GetUnreadMessages(peer_id);
  }

  // Actions
  auto AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) -> bool {
    return impl_->AddContact(peer_id, token);
  }

  void BlockContact(ContactID const& peer_id) {
    impl_->BlockContact(peer_id);
  }
  void DeleteContact(ContactID const& peer_id) {
    impl_->DeleteContact(peer_id);
  }
  void UnblockContact(ContactID const& peer_id) {
    impl_->UnblockContact(peer_id);
  }
  void AcceptContact(ContactID const& peer_id) {
    impl_->AcceptContact(peer_id);
  }
  void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& name) {
    impl_->SetContactDisplayNameOverride(peer_id, name);
  }
  void ResetContactDisplayNameOverride(ContactID const& peer_id) {
    impl_->ResetContactDisplayNameOverride(peer_id);
  }
  auto GenerateContactToken() -> std::vector<std::byte> {
    return impl_->client->GenerateContactToken();
  }
  void MarkAllAsRead(ContactID const& peer_id) {
    impl_->MarkAllAsRead(peer_id);
  }

 private:
  struct Impl : public std::enable_shared_from_this<Impl> {
    std::shared_ptr<Client> client;

    mutable std::mutex mutex;
    std::vector<ViewContact> contacts;

    explicit Impl(std::shared_ptr<Client> client)
        : client(std::move(client)) {
    }

    [[nodiscard]] auto GetContacts() const -> std::vector<ViewContact> {
      std::lock_guard<std::mutex> const lock(mutex);
      return contacts;
    }

    auto AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token) -> bool {
      if (peer_id == client->GetId()) {
        return false;
      }
      std::thread([self = shared_from_this(), peer_id, token]() { self->client->AddContact(peer_id, token); }).detach();
      return true;
    }

    void BlockContact(ContactID const& peer_id) {
      std::thread([self = shared_from_this(), peer_id]() {
        self->client->BlockContact(peer_id, true);
        self->DoUpdateContactsList();
      }).detach();
    }

    void DeleteContact(ContactID const& peer_id) {
      std::thread([self = shared_from_this(), peer_id]() {
        self->client->DeleteContact(peer_id);
        self->DoUpdateContactsList();
      }).detach();
    }

    void UnblockContact(ContactID const& peer_id) {
      std::thread([self = shared_from_this(), peer_id]() { self->client->UnblockContact(peer_id); }).detach();
    }

    void AcceptContact(ContactID const& peer_id) {
      std::thread([self = shared_from_this(), peer_id]() { self->client->ConfirmContact(peer_id); }).detach();
    }

    void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& name) {
      std::thread([self = shared_from_this(), peer_id, name]() {
        self->client->SetContactDisplayNameOverride(peer_id, name);
        self->DoUpdateContactsList();
      }).detach();
    }

    void ResetContactDisplayNameOverride(ContactID const& peer_id) {
      std::thread([self = shared_from_this(), peer_id]() {
        self->client->ResetContactDisplayNameOverride(peer_id);
        self->DoUpdateContactsList();
      }).detach();
    }

    void MarkAllAsRead(ContactID const& peer_id) {
      std::thread([self = shared_from_this(), peer_id]() {
        auto unread = self->client->GetUnreadMessages(peer_id);
        std::vector<std::uint64_t> ids;
        ids.reserve(unread.size());
        for (auto const& msg : unread) {
          ids.push_back(msg.id);
        }
        if (!ids.empty()) {
          self->client->MarkMessagesAsRead(peer_id, ids);
          self->DoUpdateContactsList();
        }
      }).detach();
    }

    void UpdateContactsList() {
      std::thread([self = shared_from_this()]() { self->DoUpdateContactsList(); }).detach();
    }

    void DoUpdateContactsList() {
      std::vector<ViewContact> temp_contacts;
      {
        auto const contacts_from_manager = client->GetContacts();
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
               .connection_state = client->GetPeerConnectionState(contact.peer_id),
               .unread_count = 0}
          );
          contact_ids.push_back(contact.peer_id);
        }

        auto const chat_summaries = client->GetChatSummaries(contact_ids);
        std::unordered_map<ContactID, messaging::ChatSummary> chat_map;
        for (auto const& chat : chat_summaries) {
          chat_map[chat.peer_id] = chat;
        }

        for (auto& contact : temp_contacts) {
          if (chat_map.contains(contact.peer_id)) {
            auto const& summary = chat_map.at(contact.peer_id);
            contact.unread_count = summary.unread_count;
            contact.last_activity_timestamp = summary.last_message.timestamp;

            contact.last_message = summary.last_message;
          } else {
            contact.unread_count = 0;
            contact.last_activity_timestamp = 0;
            contact.last_message.reset();
          }
        }

        std::ranges::sort(temp_contacts, [](auto const& left, auto const& right) {
          return left.last_activity_timestamp > right.last_activity_timestamp;
        });
      }

      {
        std::lock_guard<std::mutex> const lock(mutex);
        contacts = temp_contacts;
      }
      EventQueue::Instance().Push(EventContactsStateUpdated{temp_contacts});
    }

    void UpdateContactConnectionState(ContactID const& peer_id, transport::PeerConnectionState state) {
      std::vector<ViewContact> notify_copy;
      bool changed = false;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        for (auto& contact : contacts) {
          if (contact.peer_id == peer_id) {
            if (contact.connection_state != state) {
              contact.connection_state = state;
              changed = true;
            }
            break;
          }
        }
        if (changed) {
          notify_copy = contacts;
        }
      }
      if (changed) {
        EventQueue::Instance().Push(EventContactsStateUpdated{notify_copy});
      }
    }
  };

  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<EventQueue::SubscriberTypeId> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
    auto id = queue.Subscribe<EventType>(std::move(callback));
    eventUnsubscribers_.push_back(id);
  }
};

}  // namespace eillance::view_model
