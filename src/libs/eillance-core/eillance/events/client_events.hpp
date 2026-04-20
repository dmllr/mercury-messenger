#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/events/service_connection_state.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/topic_id.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/data_types.hpp"
#include "eillance/transport/peer_connection_state.hpp"

namespace eillance {

struct EventFrameReceived {
  ContactID peer_id;
  std::vector<std::byte> frame;
};

struct EventDataChannelOpen {
  ContactID peer_id;
};

struct EventFatalError {};

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
struct EventDataChannelMessage {
  ContactID peer_id;
  std::vector<std::byte> message;
};
struct EventDebugDataReset {};
struct EventDataReloaded {};

struct EventPeerVersionMismatch {
  ContactID peer_id;
  bool we_are_outdated;
};

// --- Ledger Sync Events ---
struct EventLedgerAppended {
  storage::LedgerEntry entry;
};

// Orchestration Events replacing cross-service calls
struct EventConnectionAttemptFinished {
  ContactID peer_id;
};
struct EventDebugClearContactCache {};
struct EventReloadContactCache {};
struct EventSendQueuedMessages {
  ContactID peer_id;
};

// Application Lifecycle
struct EventAppForegrounded {};
struct EventAppBackgrounded {};

// Permissions Integration
enum class PermissionBannerType : uint8_t { none = 0, notification = 1, battery = 2, callPermissions = 3 };
struct EventPermissionBannerChanged {
  PermissionBannerType type;
};

// Explicit Notification Triggers
struct EventNotifyNewMessage {
  ContactID peer_id;
  uint64_t message_id;
  std::string contact_name;
  std::vector<messaging::HistoryEntryRecord> unread_messages;
};
struct EventNotifyMissedCall {
  ContactID peer_id;
  std::string contact_name;
};
struct EventNotifyContactRequest {
  ContactID peer_id;
  std::string contact_name;
};

// Explicit Hardware / OS Orchestration Commands
enum class AudioRoutingMode : uint8_t { normal = 0, inCall = 1 };
enum class SoundType : uint8_t { ringing = 0, ringback = 1, connecting = 2 };

struct EventCmdProcessIncomingTelecomCall {
  ContactID peer_id;
};
struct EventCmdProcessOutgoingTelecomCall {
  ContactID peer_id;
  bool is_video;
};
struct EventCmdDisconnectTelecomCall {
  ContactID peer_id;
};
struct EventCmdSetTelecomCallActive {
  ContactID peer_id;
};
struct EventCmdSetAudioMode {
  AudioRoutingMode mode;
};
struct EventCmdClearCommunicationDevice {};
struct EventCmdPlaySound {
  SoundType type;
};
struct EventCmdStopSound {};
struct EventCmdSetMicrophoneMute {
  bool is_muted;
};
struct EventCmdSetSpeakerphone {
  bool is_speaker_on;
};

// WebRTC Media signaling bridge event
struct EventSendMediaSignalingMessage {
  nlohmann::json message;
};

}  // namespace eillance
