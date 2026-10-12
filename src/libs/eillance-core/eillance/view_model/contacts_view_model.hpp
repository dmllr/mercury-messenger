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
    Subscribe<EventDataReloaded>(queue, [impl](auto const&) { impl->UpdateContactsList(); });
    Subscribe<EventGroupsStateUpdated>(queue, [impl](auto const&) { impl->UpdateContactsList(); });

    Subscribe<EventDataPeerConnectionStateChanged>(queue, [impl](auto const& event) {
      impl->UpdateContactConnectionState(event.peer_id, event.state);
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

  [[nodiscard]] auto GetUnreadMessages(TopicID const& topic_id) const -> std::vector<messaging::HistoryEntryRecord> {
    return impl_->client->GetUnreadMessages(topic_id);
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
  void MarkAllAsRead(TopicID const& topic_id) {
    impl_->MarkAllAsRead(topic_id);
  }

  auto GetGroupMembers(std::string const& group_id) -> std::vector<identity::GroupMember> {
    return impl_->client->GetGroupMembers(GroupID(group_id));
  }
  bool AddGroupMember(std::string const& group_id, std::string const& contact_id) {
    return impl_->client->AddGroupMember(GroupID(group_id), ContactID(contact_id));
  }
  bool RemoveGroupMember(std::string const& group_id, std::string const& contact_id) {
    return impl_->client->RemoveGroupMember(GroupID(group_id), ContactID(contact_id));
  }
  void LeaveGroup(std::string const& group_id) {
    impl_->LeaveGroup(GroupID(group_id));
  }
  void UnsubscribeChannel(std::string const& channel_id) {
    impl_->UnsubscribeChannel(ChannelID(channel_id));
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

    void LeaveGroup(GroupID const& group_id) {
      std::thread([self = shared_from_this(), group_id]() {
        self->client->LeaveGroup(group_id);
        self->DoUpdateContactsList();
      }).detach();
    }

    void UnsubscribeChannel(ChannelID const& channel_id) {
      std::thread([self = shared_from_this(), channel_id]() {
        self->client->UnsubscribeChannel(channel_id);
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

    void MarkAllAsRead(TopicID const& topic_id) {
      std::thread([self = shared_from_this(), topic_id]() {
        auto unread = self->client->GetUnreadMessages(topic_id);
        std::vector<std::uint64_t> ids;
        ids.reserve(unread.size());
        for (auto const& msg : unread) {
          ids.push_back(msg.id);
        }
        if (!ids.empty()) {
          self->client->MarkMessagesAsRead(topic_id, ids);
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
        std::vector<TopicID> topic_ids;
        topic_ids.reserve(contacts_from_manager.size());

        for (auto const& contact : contacts_from_manager) {
          std::string display_name;
          if (contact.display_name_override) {
            display_name = *contact.display_name_override;
          } else if (contact.display_name) {
            display_name = *contact.display_name;
          }

          temp_contacts.push_back(
              {.topic_id = TopicID(contact.peer_id),
               .display_name = display_name,
               .bio = contact.bio,
               .photo = contact.photo_base64,
               .status = contact.status,
               .connection_state = client->GetPeerConnectionState(contact.peer_id),
               .unread_count = 0,
               .last_message = std::nullopt,
               .last_activity_timestamp = 0,
               .is_group = false,
               .is_channel = false,
               .is_read_only = false,
               .owner_id = std::nullopt}
          );
          topic_ids.push_back(TopicID(contact.peer_id));
        }

        for (auto const& group : client->GetGroups()) {
          temp_contacts.push_back(
              {.topic_id = TopicID(group.id),
               .display_name = group.name,
               .bio = "Group Chat",
               .photo = group.photo_base64,
               .status = identity::ContactStatus::confirmed,
               .connection_state = transport::PeerConnectionState::disconnected,
               .unread_count = 0,
               .last_message = std::nullopt,
               .last_activity_timestamp = 0,
               .is_group = true,
               .is_channel = false,
               .is_read_only = false,
               .owner_id = group.owner_id}
          );
          topic_ids.push_back(TopicID(group.id));
        }

        for (auto const& channel : client->GetChannels()) {
          bool is_read_only = (channel.owner_id != client->GetId());
          if (is_read_only) {
            for (auto const& m : channel.members) {
              if (m.contact_id == client->GetId() && m.role == 1) {
                is_read_only = false;
                break;
              }
            }
          }
          temp_contacts.push_back(
              {.topic_id = TopicID(channel.id),
               .display_name = channel.name,
               .bio = "Channel",
               .photo = std::nullopt,
               .status = identity::ContactStatus::confirmed,
               .connection_state = transport::PeerConnectionState::disconnected,
               .unread_count = 0,
               .last_message = std::nullopt,
               .last_activity_timestamp = 0,
               .is_group = false,
               .is_channel = true,
               .is_read_only = is_read_only,
               .owner_id = channel.owner_id}
          );
          topic_ids.push_back(TopicID(channel.id));
        }

        auto const chat_summaries = client->GetChatSummaries(topic_ids);
        std::unordered_map<TopicID, messaging::ChatSummary> chat_map;
        for (auto const& chat : chat_summaries) {
          chat_map[chat.topic_id] = chat;
        }

        for (auto& contact : temp_contacts) {
          if (chat_map.contains(contact.topic_id)) {
            auto const& summary = chat_map.at(contact.topic_id);
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
      UpdateContactsList();
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
