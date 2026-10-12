#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "eillance/events/service_connection_state.hpp"
#include "eillance/identity/contact_id.hpp"
#include "eillance/messaging/types.hpp"
#include "eillance/storage/data_types.hpp"
#include "eillance/transport/peer_connection_state.hpp"

namespace eillance::view_model {

struct CallState {
  bool is_connecting = false;
  bool is_in_call = false;
  bool is_ringing = false;
  messaging::CallDirection call_direction = messaging::CallDirection::outgoing;
  ContactID peer_id;
  messaging::HistoryEntryKind call_type = messaging::HistoryEntryKind::voiceCall;
  bool is_muted = false;
  bool is_speaker_on = false;
  uint32_t duration_seconds = 0;
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

struct ViewContact {
  ContactID peer_id;
  std::string display_name;
  std::optional<std::string> bio;
  std::optional<std::string> photo;
  identity::ContactStatus status;
  transport::PeerConnectionState connection_state = transport::PeerConnectionState::disconnected;
  uint32_t unread_count = 0;
  std::optional<messaging::HistoryEntryRecord> last_message;
  uint64_t last_activity_timestamp = 0;
};

struct DraftAttachment {
  std::string file_path;
  messaging::AttachmentType type;
  std::string mime_type;
  std::string file_name;
  std::string thumbnail_base64;
  std::string note;
};

struct ConversationState {
  ContactID peer_id;
  std::deque<messaging::HistoryEntryRecord> history{};
  bool has_older_messages = false;
  std::vector<DraftAttachment> drafts{};
};

struct SettingsState {
  std::optional<std::string> signaling_server_url;
  int peer_inactivity_timeout_seconds = 120;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::optional<std::string> custom_stun;
  std::optional<std::string> custom_turn;
  std::optional<std::string> custom_turn_user;
  std::optional<std::string> custom_turn_pass;
  bool play_sound_on_send = true;
  bool play_sound_on_receive = true;
  bool show_notification_content = true;
};

// --- ViewModel Events ---
struct EventContactsStateUpdated {
  std::vector<ViewContact> contacts;
};
struct EventConversationStateUpdated {
  ContactID peer_id;
  ConversationState state;
};
struct EventConversationDraftsUpdated {
  ContactID peer_id;
  std::vector<DraftAttachment> drafts;
};
struct EventTransferProgressUpdated {
  ContactID peer_id;
  std::string attachment_id;
  float progress;
};
struct EventCallStateChanged {
  CallState state;
};
struct EventProfileStateUpdated {
  MyProfile profile;
};
struct EventSettingsStateUpdated {
  SettingsState state;
};
struct EventConnectionStateChanged {
  ServiceConnectionState state;
};

}  // namespace eillance::view_model
