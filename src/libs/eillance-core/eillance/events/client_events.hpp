#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "eillance/events/event_source.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/data_types.hpp"

namespace eillance {

struct EventFrameReceived {
  std::vector<std::byte> frame;
};
struct EventChatMessageReceived {
  ContactID peer_id;
  messaging::HistoryEntryRecord message;
};
struct EventContactRequest {
  ContactID peer_id;
};
struct EventContactRestored {
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
struct EventMessageUpdated {
  ContactID peer_id;
  std::uint64_t message_id;
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
  int state;
};
struct EventIncomingCall {
  ContactID peer_id;
  messaging::HistoryEntryKind call_type;
};
struct EventOutgoingCall {
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
struct EventConnectionRequested {
  ContactID peer_id;
};
struct EventServerIceConfig {
  std::string config_json;
};
struct EventDataChannelMessage {
  ContactID peer_id;
  std::vector<std::byte> message;
};
struct EventDebugDataReset {};
struct EventDataReloaded {};

}  // namespace eillance
