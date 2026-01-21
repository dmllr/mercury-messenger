#pragma once

#include <memory>
#include <utility>

#include "eillance/client/client.hpp"
#include "eillance/client/device.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::view_model {

// Events for the internal bus
struct EventFrameReceived {
  std::vector<std::byte> frame{};
};
struct EventProfileReceived {
  ContactID peer_id;
  Device::Profile profile{};
};
struct EventChatMessageReceived {
  ContactID peer_id;
  messaging::HistoryEntryRecord message;
};
struct EventContactRequest {
  ContactID peer_id;
};
struct EventContactStatusChanged {
  ContactID peer_id;
  identity::ContactStatus status;
};
struct EventMessageStatusChanged {
  ContactID peer_id;
  std::uint64_t message_id{};
  messaging::MessageStatus status;
};
struct EventPeerListUpdate {
  std::vector<Device> peers{};
};
struct EventStatusChanged {
  ContactID peer_id;
  EventSource source;
  int state{};
};
struct EventIncomingCall {
  ContactID peer_id;
  messaging::HistoryEntryKind call_type;
};
struct EventCallRinging {
  ContactID peer_id;
};
struct EventCallAccepted {
  ContactID peer_id;
};
struct EventCallTerminated {
  ContactID peer_id;
};
struct EventMyProfileUpdated {};
struct EventContactProfileUpdated {
  ContactID peer_id;
};
struct EventContactRemoved {
  ContactID peer_id;
};
struct EventDebugDataReset {};

// Proxies events from the Client services to a multi-listener EventQueue.
class ClientEventRouter {
  std::shared_ptr<Client> client_;
  EventQueue& eventQueue_;

 public:
  explicit ClientEventRouter(std::shared_ptr<Client> client)
      : client_(std::move(client)),
        eventQueue_(EventQueue::Instance()) {
    AttachToClient();
  }

  auto Queue() -> EventQueue& { return eventQueue_; }

 private:
  void AttachToClient() {
    auto messaging = client_->GetMessagingService();
    auto contacts = client_->GetContactService();
    auto media = client_->GetMediaCallService();
    auto account = client_->GetAccountService();

    // Messaging
    messaging->SetChatMessageReceivedCallback([this](auto const& id, auto const& msg) {
      eventQueue_.Push(EventChatMessageReceived{id, msg});
    });
    messaging->SetMessageStatusChangedCallback([this](auto const& peer, auto msg_id, auto status) {
      eventQueue_.Push(EventMessageStatusChanged{peer, msg_id, status});
    });

    // Contacts
    contacts->SetContactRequestCallback([this](auto const& id) { eventQueue_.Push(EventContactRequest{id}); });
    contacts->SetContactStatusChangedCallback([this](auto const& id, auto status) {
      eventQueue_.Push(EventContactStatusChanged{id, status});
    });
    contacts->SetContactProfileUpdatedCallback([this](auto const& id) {
      eventQueue_.Push(EventContactProfileUpdated{id});
    });
    contacts->SetContactRemovedCallback([this](auto const& id) { eventQueue_.Push(EventContactRemoved{id}); });
    contacts->SetPeerListUpdateCallback([this](auto const& list) { eventQueue_.Push(EventPeerListUpdate{list}); });
    contacts->SetProfileReceivedCallback([this](auto const& id, auto const& p) {
      eventQueue_.Push(EventProfileReceived{id, p});
    });

    // Media
    media->SetFrameReceivedCallback([this](auto const& frame) { eventQueue_.Push(EventFrameReceived{frame}); });
    media->SetIncomingCallCallback([this](auto const& id, auto type) { eventQueue_.Push(EventIncomingCall{id, type}); }
    );
    media->SetCallRingingCallback([this](auto const& id) { eventQueue_.Push(EventCallRinging{id}); });
    media->SetCallAcceptedCallback([this](auto const& id) { eventQueue_.Push(EventCallAccepted{id}); });
    media->SetCallTerminatedCallback([this](auto const& id) { eventQueue_.Push(EventCallTerminated{id}); });

    // Account
    account->SetOnMyProfileUpdatedCallback([this]() { eventQueue_.Push(EventMyProfileUpdated{}); });

    // Legacy Client / Transport status
    client_->SetStatusCallback([this](auto const& id, auto src, auto state) {
      eventQueue_.Push(EventStatusChanged{id, src, state});
    });
  }
};

}  // namespace eillance::view_model
