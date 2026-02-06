#pragma once

#include <memory>
#include <utility>

#include "eillance/client/client.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::view_model {

// Events for the internal bus
struct EventFrameReceived {
  std::vector<std::byte> frame{};
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
struct EventAttachmentDownloaded {
  ContactID peer_id;
  std::string attachment_id;
};
struct EventTransferProgress {
  ContactID peer_id;
  std::string attachment_id;
  float progress;
};
struct EventAttachmentStatusChanged {
  ContactID peer_id;
  std::string attachment_id;
  messaging::AttachmentStatus status;
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
struct EventDataReloaded {};

class ClientEventRouter {
  std::shared_ptr<Client> client_;
  EventQueue& eventQueue_;

 public:
  explicit ClientEventRouter(std::shared_ptr<Client> client)
      : client_(std::move(client)),
        eventQueue_(EventQueue::Instance()) {
    AttachToClient();
  }

  ~ClientEventRouter() {
    DetachFromClient();
  }

  auto Queue() -> EventQueue& {
    return eventQueue_;
  }

  void DetachFromClient() {
    if (!client_) return;
    auto messaging = client_->GetMessagingService();
    auto contacts = client_->GetContactService();
    auto media = client_->GetMediaCallService();
    auto account = client_->GetAccountService();
    auto profile = client_->GetProfileService();

    // NOLINTBEGIN(*-missing-std-forward)
    messaging->SetChatMessageReceivedCallback([](auto&&, auto&&) {});
    messaging->SetMessageStatusChangedCallback([](auto&&, auto, auto) {});
    messaging->SetAttachmentDownloadedCallback([](auto&&, auto&&) {});
    messaging->SetTransferProgressCallback([](auto&&, auto&&, auto) {});
    messaging->SetAttachmentStatusChangedCallback([](auto&&, auto&&, auto) {});

    contacts->SetContactRequestCallback([](auto&&) {});
    contacts->SetContactStatusChangedCallback([](auto&&, auto) {});
    contacts->SetContactRemovedCallback([](auto&&) {});

    profile->SetContactProfileUpdatedCallback([](auto&&) {});

    media->SetFrameReceivedCallback([](auto&&) {});
    media->SetIncomingCallCallback([](auto&&, auto) {});
    media->SetCallRingingCallback([](auto&&) {});
    media->SetCallAcceptedCallback([](auto&&) {});
    media->SetCallTerminatedCallback([](auto&&) {});
    account->SetOnMyProfileUpdatedCallback([]() {});
    client_->SetStatusCallback([](auto&&, auto, auto) {});
    client_->SetDataReloadedCallback([]() {});
    // NOLINTEND(*-missing-std-forward)
  }

 private:
  void AttachToClient() {
    auto messaging = client_->GetMessagingService();
    auto contacts = client_->GetContactService();
    auto media = client_->GetMediaCallService();
    auto account = client_->GetAccountService();
    auto profile = client_->GetProfileService();

    messaging->SetChatMessageReceivedCallback([this](auto const& id, auto const& msg) {
      eventQueue_.Push(EventChatMessageReceived{id, msg});
    });
    messaging->SetMessageStatusChangedCallback([this](auto const& peer, auto msg_id, auto status) {
      eventQueue_.Push(EventMessageStatusChanged{peer, msg_id, status});
    });
    messaging->SetAttachmentDownloadedCallback([this](auto const& peer, auto const& att_id) {
      eventQueue_.Push(EventAttachmentDownloaded{peer, att_id});
    });
    messaging->SetTransferProgressCallback([this](auto const& peer, auto const& att_id, float progress) {
      eventQueue_.Push(EventTransferProgress{peer, att_id, progress});
    });
    messaging->SetAttachmentStatusChangedCallback([this](auto const& peer, auto const& att_id, auto status) {
      eventQueue_.Push(EventAttachmentStatusChanged{peer, att_id, status});
    });

    contacts->SetContactRequestCallback([this](auto const& id) { eventQueue_.Push(EventContactRequest{id}); });
    contacts->SetContactStatusChangedCallback([this](auto const& id, auto status) {
      eventQueue_.Push(EventContactStatusChanged{id, status});
    });
    contacts->SetContactRemovedCallback([this](auto const& id) { eventQueue_.Push(EventContactRemoved{id}); });

    profile->SetContactProfileUpdatedCallback([this](auto const& id) {
      eventQueue_.Push(EventContactProfileUpdated{id});
    });

    media->SetFrameReceivedCallback([this](auto const& frame) { eventQueue_.Push(EventFrameReceived{frame}); });
    media->SetIncomingCallCallback([this](auto const& id, auto type) { eventQueue_.Push(EventIncomingCall{id, type}); }
    );
    media->SetCallRingingCallback([this](auto const& id) { eventQueue_.Push(EventCallRinging{id}); });
    media->SetCallAcceptedCallback([this](auto const& id) { eventQueue_.Push(EventCallAccepted{id}); });
    media->SetCallTerminatedCallback([this](auto const& id) { eventQueue_.Push(EventCallTerminated{id}); });
    account->SetOnMyProfileUpdatedCallback([this]() { eventQueue_.Push(EventMyProfileUpdated{}); });
    client_->SetStatusCallback([this](auto const& id, auto src, auto state) {
      eventQueue_.Push(EventStatusChanged{id, src, state});
    });
    client_->SetDataReloadedCallback([this]() { eventQueue_.Push(EventDataReloaded{}); });
  }
};

}  // namespace eillance::view_model
