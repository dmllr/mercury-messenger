#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/events/service_connection_state.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/data_types.hpp"
#include "eillance/transport/peer_connection_state.hpp"

namespace eillance {

struct EventFrameReceived {
  ContactID peer_id;
  std::vector<std::byte> frame;
};

struct EventSignalingError {
  ContactID peer_id;
  std::string error;
};

struct EventLoginSuccess {};

struct EventPeerMessage {
  nlohmann::json message;
};

struct EventSystemMessage {
  std::string type;
  nlohmann::json message;
};

struct EventDataChannelOpen {
  ContactID peer_id;
};

struct EventFatalError {};

struct EventMediaSample {
  uint8_t type;
  uint64_t timestamp;
  std::vector<std::byte> sample;
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

struct EventDataPeerConnectionStateChanged {
  ContactID peer_id;
  transport::PeerConnectionState state;
};
struct EventMediaPeerConnectionStateChanged {
  ContactID peer_id;
  transport::PeerConnectionState state;
};
struct EventServiceConnectionStateChanged {
  ServiceConnectionState state;
};
struct EventMediaGatheringStateChanged {
  ContactID peer_id;
  int state_val;
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
struct EventPeerActivity {
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

// Orchestration Events replacing cross-service calls
struct EventConnectionAttemptFinished {
  ContactID peer_id;
};
struct EventStopDeliveryService {};
struct EventDebugClearContactCache {};
struct EventReloadContactCache {};
struct EventSendQueuedMessages {
  ContactID peer_id;
};

}  // namespace eillance
