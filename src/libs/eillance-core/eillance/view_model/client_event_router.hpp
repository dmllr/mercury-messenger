#pragma once

#include <memory>
#include <utility>

#include "eillance/client/client.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::view_model {

// Events for the internal bus
struct EventFrameReceived {
  std::vector<std::byte> frame;
};
struct EventProfileReceived {
  ContactID peer_id;
  Device::Profile profile;
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
  std::uint64_t message_id;
  messaging::MessageStatus status;
};
struct EventPeerListUpdate {
  std::vector<Device> peers;
};
struct EventStatusChanged {
  ContactID peer_id;
  EventSource source;
  int state;
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

// Proxies events from the Client's single-callback system to a multi-listener EventQueue.
class ClientEventRouter {
  std::shared_ptr<Client> client_;
  EventQueue& event_queue_;

public:
  explicit ClientEventRouter(std::shared_ptr<Client> client)
      : client_(std::move(client)), event_queue_(EventQueue::Instance()) {
    AttachToClient();
  }

  // Allow sub-models to subscribe to the queue directly
  auto Queue() -> EventQueue& { return event_queue_; }

private:
  void AttachToClient() {
    client_->SetFrameReceivedCallback([this](const auto& frame) {
      event_queue_.Push(EventFrameReceived{frame});
    });
    client_->SetProfileReceivedCallback([this](const auto& id, const auto& p) {
      event_queue_.Push(EventProfileReceived{id, p});
    });
    client_->SetChatMessageReceivedCallback([this](const auto& id, const auto& msg) {
      event_queue_.Push(EventChatMessageReceived{id, msg});
    });
    client_->SetContactRequestCallback([this](const auto& id) {
      event_queue_.Push(EventContactRequest{id});
    });
    client_->SetContactStatusChangedCallback([this](const auto& id, auto status) {
      event_queue_.Push(EventContactStatusChanged{id, status});
    });
    client_->SetMessageStatusChangedCallback([this](const auto& peer, auto msg_id, auto status) {
      event_queue_.Push(EventMessageStatusChanged{peer, msg_id, status});
    });
    client_->SetPeerListUpdateCallback([this](const auto& list) {
      event_queue_.Push(EventPeerListUpdate{list});
    });
    client_->SetStatusCallback([this](const auto& id, auto src, auto state) {
      event_queue_.Push(EventStatusChanged{id, src, state});
    });
    client_->SetIncomingCallCallback([this](const auto& id, auto type) {
      event_queue_.Push(EventIncomingCall{id, type});
    });
    client_->SetOnCallRingingCallback([this](const auto& id) {
      event_queue_.Push(EventCallRinging{id});
    });
    client_->SetOnCallAcceptedCallback([this](const auto& id) {
      event_queue_.Push(EventCallAccepted{id});
    });
    client_->SetOnCallTerminatedCallback([this](const auto& id) {
      event_queue_.Push(EventCallTerminated{id});
    });
    client_->SetOnMyProfileUpdatedCallback([this]() {
      event_queue_.Push(EventMyProfileUpdated{});
    });
    client_->SetOnContactProfileUpdatedCallback([this](const auto& id) {
      event_queue_.Push(EventContactProfileUpdated{id});
    });
    client_->SetOnContactRemovedCallback([this](const auto& id) {
      event_queue_.Push(EventContactRemoved{id});
    });
  }
};

}  // namespace eillance::view_model
