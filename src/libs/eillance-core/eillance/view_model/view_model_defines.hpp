#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/contact_manager.hpp"  // for ContactStatus
#include "eillance/messaging/history.hpp"
#include "eillance/transport/peer_connection_state.hpp"

namespace eillance::view_model {

struct ViewContact {
  ContactID peer_id;
  std::string display_name;
  std::optional<std::string> bio;
  std::optional<std::string> photo;  // base64
  identity::ContactStatus status;
  eillance::transport::PeerConnectionState connection_state = eillance::transport::PeerConnectionState::disconnected;
  uint32_t unread_count = 0;
  std::optional<messaging::HistoryEntryRecord> last_message;
  uint64_t last_activity_timestamp = 0;
};

struct CallState {
  bool is_connecting = false;
  bool is_in_call = false;
  bool is_ringing = false;
  messaging::CallDirection call_direction = messaging::CallDirection::outgoing;
  ContactID peer_id;
  messaging::HistoryEntryKind call_type = messaging::HistoryEntryKind::voiceCall;
};

struct MyProfile {
  ContactID my_id;
  bool identity_exists = true;
  std::optional<std::string> my_display_name;
  std::optional<std::string> my_bio;
  std::optional<std::string> my_photo;  // base64
};

struct DebugPendingAction {
  ContactID peer_id;
  int64_t id;
  std::string payload;
};

struct DebugConnectionInfo {
  ContactID peer_id;
  eillance::transport::PeerConnectionState state;
  int64_t last_activity_sec_ago;
  bool is_data_channel_open;
  bool has_media_connection;
};

struct DebugUnreadInfo {
  ContactID peer_id;
  uint32_t count;
  uint64_t last_msg_timestamp;
};

}  // namespace eillance::view_model
