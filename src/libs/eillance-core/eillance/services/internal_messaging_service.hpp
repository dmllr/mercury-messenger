#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/events/client_events.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/services/sync_engine.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/transport/media_transport.hpp"
#include "eillance/transport/protocol.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::services {

class InternalMessagingService {
 public:
  using Json = nlohmann::json;

  explicit InternalMessagingService(ServiceManager* sm)
      : sm_(sm),
        data_transport_(sm->data_transport),
        media_transport_(sm->media_transport) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelMessage>(
        [this](EventDataChannelMessage const& e) { this->HandleDataChannelMessage(e.peer_id, e.message); }
    ));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) {
      this->OnDataChannelOpen(e.peer_id);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventSendMediaSignalingMessage>([this](auto const& e) {
      std::string const to_str = e.message.value("to", "");
      if (!to_str.empty()) {
        ContactID const peer_id(to_str);
        if (auto p = data_transport_->GetPeer(peer_id)) {
          p->pending_offer_sdp = "";  // Mark as sent to prevent forced fallback
        }
        std::ignore = data_transport_->SendJson(peer_id, e.message);
      }
    }));
  }

  ~InternalMessagingService() {
    for (auto const id : event_unsubs_) EventQueue::Instance().Unsubscribe(id);
  }

  void HandleDataChannelMessage(ContactID const& peer_id, std::span<std::byte const> const msg) {
    if (auto p = data_transport_->GetPeer(peer_id)) {
      p->last_activity_time = Time::Now();
    }

    if (!msg.empty() && msg[0] == std::byte{0xFE}) {
      // Ignored here, FileTransferService subscribes to this directly and checks for 0xFE.
    } else {
      try {
        std::string json_str;
        json_str.resize(msg.size());
        std::memcpy(json_str.data(), msg.data(), msg.size());
        ProcessDataChannelMessage(peer_id, Json::parse(json_str));
      } catch (...) {
        LGE << "Failed to parse data channel message from " << peer_id.ToString();
      }
    }

    bool should_unsuspend = false;
    {
      auto const now = Time::Now();
      std::lock_guard<std::mutex> const lock(unsuspend_mutex_);
      auto const last = last_unsuspend_times_[peer_id];
      if (now > last && (now - last) >= kUnsuspendInterval) {
        last_unsuspend_times_[peer_id] = now;
        should_unsuspend = true;
      }
    }
    if (should_unsuspend) {
      EventQueue::Instance().Push(EventPeerActivity{peer_id});
    }
  }

  void ProcessDataChannelMessage(ContactID const& peer_id, Json const& message) {
    if (!message.contains("type")) {
      return;
    }

    std::string const type = message["type"];

    if (type == "key_exchange") {
      HandleKeyExchange(peer_id, message);
    } else if (type == "hangup") {
      sm_->media_call->OnHangupSignal(peer_id, message.value("call_id", ""));
    } else if (type == "call_request") {
      auto kind = static_cast<messaging::HistoryEntryKind>(message.value("kind", 0));
      sm_->media_call->OnCallRequest(peer_id, message.value("call_id", ""), kind);
    } else if (type == "call_reject") {
      sm_->media_call->OnCallReject(peer_id, message.value("call_id", ""));
    } else if (type == "call_accept") {
      sm_->media_call->OnCallAccepted(peer_id, message.value("call_id", ""));
    } else if (type == "call_ringing") {
      sm_->media_call->OnCallRinging(peer_id, message.value("call_id", ""));
    } else if (type == transport::protocol::values::offer || type == transport::protocol::values::answer) {
      HandleSdp(peer_id, type, message);
    } else if (type == "termination") {
      data_transport_->RemovePeer(peer_id);
    } else if (type.starts_with("sync_")) {
      CheckPeerVersion(peer_id, message.value("sync_version", currentSyncVersion));
      sm_->sync_engine->ProcessSyncMessage(peer_id, message);
    } else if (type == "chat" || type == "contact_add_request" || type == "contact_add_ack" ||
               type == "contact_confirm" || type == "contact_confirm_ack" || type == "contact_removed" ||
               type == "contact_removed_ack" || type == "profile_info" || type == "profile_request" ||
               type == "message_read" || type == "message_receipt" || type == "file_cancel" ||
               type == "message_delete" || type == "message_edit" || type == "message_reaction") {
      CheckPeerVersion(peer_id, 0);
    }
  }

  void OnDataChannelOpen(ContactID const& peer_id) {
    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      return;
    }

    if (peer->data_channel_initialized.exchange(true)) {
      LGI << "Data channel with " << peer_id.ToString() << " is already initialized. Ignoring duplicate event.";
      return;
    }

    LGI << "Data channel with " << peer_id.ToString() << " opened.";

    peer->session_cipher->Reset();
    peer->last_activity_time = Time::Now();

    {
      std::lock_guard<std::mutex> const lock(unsuspend_mutex_);
      last_unsuspend_times_[peer_id] = Time::Now();
    }

    if (sm_->account->GetId() > peer_id) {
      auto my_pub_key = peer->session_cipher->GetPublicKey();
      Json const key_msg = {
          {"type", "key_exchange"},
          {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
          {"is_reply", false},
          {"sync_version", currentSyncVersion}
      };
      std::ignore = data_transport_->SendJson(peer_id, key_msg);
    }
  }

 private:
  void HandleSdp(ContactID const& peer_id, std::string const& type, Json const& message) {
    std::string const context_str =
        message.value(transport::protocol::keys::context, transport::protocol::values::contextData);
    bool const is_media = (context_str == transport::protocol::values::contextMedia);

    if (!is_media) {
      LGD << "[Signaling] Ignoring non-media SDP " << type << " from " << peer_id.ToString();
      return;  // We no longer negotiate DataChannels via SDP, libp2p handles data connections natively
    }
    LGI << "[Signaling] Received media SDP " << type << " from " << peer_id.ToString();

    if (auto peer = data_transport_->GetPeer(peer_id); !peer) {
      if (type == transport::protocol::values::offer) {
        peer = std::make_shared<transport::Peer>(peer_id);
        data_transport_->AddPeer(peer_id, peer);
      } else {
        return;
      }
    } else {
      peer->last_activity_time = Time::Now();
    }

    if (type == transport::protocol::values::offer) {
      auto const sdp = message[transport::protocol::keys::sdp].get<std::string>();
      sm_->media_call->OnMediaOffer(peer_id, sdp);
    } else if (type == transport::protocol::values::answer || type == transport::protocol::values::prAnswer) {
      sm_->media_call->OnMediaAnswer(peer_id, message[transport::protocol::keys::sdp].get<std::string>());
    }
  }

  void HandleKeyExchange(ContactID const& peer_id, Json const& message) {
    auto const peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      return;
    }

    int const peer_sync_version = message.value("sync_version", 0);
    CheckPeerVersion(peer_id, peer_sync_version);

    auto pub_key = message["key"].get<std::vector<std::byte>>();
    if (peer->session_cipher->ComputeSharedSecret(pub_key)) {
      if (!message.value("is_reply", false)) {
        auto my_key = peer->session_cipher->GetPublicKey();
        data_transport_->SendJson(
            peer_id,
            {{"type", "key_exchange"},
             {"key", std::vector<std::byte>(my_key.begin(), my_key.end())},
             {"is_reply", true},
             {"sync_version", currentSyncVersion}}
        );
      }
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      LGE << "Failed key exchange with " << peer_id.ToString();
    }
  }

  static constexpr int currentSyncVersion = 1;

  void CheckPeerVersion(ContactID const& peer_id, int peer_sync_version) {
    if (peer_sync_version < currentSyncVersion) {
      EventQueue::Instance().Push(EventPeerVersionMismatch{peer_id, false});
      SendLegacyUpdatePrompt(peer_id);
    } else if (peer_sync_version > currentSyncVersion) {
      EventQueue::Instance().Push(EventPeerVersionMismatch{peer_id, true});
    }
  }

  void SendLegacyUpdatePrompt(ContactID const& peer_id) {
    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer || !peer->session_cipher->IsReady()) {
      return;
    }
    if (peer->legacy_prompt_sent.exchange(true)) {
      return;
    }

    nlohmann::json payload_json;
    payload_json["v"] = services::MessagingService::protocolVersion;
    payload_json["c"] = nlohmann::json{{"action", "unsupported_format"}}.dump();

    auto encrypted = peer->session_cipher->Encrypt(payload_json.dump());
    if (encrypted.empty()) {
      return;
    }

    nlohmann::json const json_msg = {
        {"type", "chat"},
        {"message_id", messaging::GenerateMessageId()},
        {"is_system", true},
        {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
    };
    std::ignore = data_transport_->SendJson(peer_id, json_msg);
  }

  ServiceManager* sm_;
  std::shared_ptr<transport::DataTransport> data_transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;

  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;

  std::unordered_map<ContactID, Time::Unit> last_unsuspend_times_;
  std::mutex unsuspend_mutex_;
  static constexpr Time::Unit kUnsuspendInterval = 5 * Time::uS;
};

}  // namespace eillance::services
