#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/messaging/history.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
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
    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventDataChannelMessage>([this](EventDataChannelMessage const& e) -> void {
          this->HandleDataChannelMessage(e.peer_id, e.message);
        })
    );

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) -> void {
      this->OnDataChannelOpen(e.peer_id);
    }));

    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventSendMediaNegotiationMessage>([this](auto const& e) -> void {
          std::string const to_str = e.message.value("to", "");
          if (!to_str.empty()) {
            ContactID const peer_id(to_str);
            if (auto p = data_transport_->GetPeer(peer_id)) {
              p->pending_offer_sdp = "";  // Mark as sent to prevent forced fallback
            }
            auto contact = sm_->contact_manager->GetContact(peer_id);
            if (!contact || contact->status != identity::ContactStatus::confirmed) {
              return;
            }

            std::ignore = data_transport_->SendJson(peer_id, e.message);
          }
        })
    );

    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventDataPeerConnectionStateChanged>([this](auto const& e) -> void {
          if (e.state == transport::PeerConnectionState::disconnected ||
              e.state == transport::PeerConnectionState::failed || e.state == transport::PeerConnectionState::closed) {
            auto const current_state = data_transport_->GetConnectionState(e.peer_id);
            if (current_state != transport::PeerConnectionState::failed &&
                current_state != transport::PeerConnectionState::closed &&
                current_state != transport::PeerConnectionState::disconnected) {
              return;
            }
            if (auto peer = data_transport_->GetPeer(e.peer_id)) {
              peer->data_channel_initialized = false;
              peer->crcp_verified = false;
              peer->legacy_prompt_sent = false;
            }
          } else if (e.state == transport::PeerConnectionState::connecting ||
                     e.state == transport::PeerConnectionState::newConnection) {
            if (auto peer = data_transport_->GetPeer(e.peer_id)) {
              peer->data_channel_initialized = false;
              peer->crcp_verified = false;
              peer->legacy_prompt_sent = false;
            }
          }
        })
    );
  }

  ~InternalMessagingService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

  void HandleDataChannelMessage(ContactID const& peer_id, std::span<std::byte const> const msg) {
    if (auto peer = data_transport_->GetPeer(peer_id)) {
      peer->last_activity_time = Time::SteadyNow();
    }

    if (msg.empty() || msg[0] != std::byte{0xFE}) {
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
      auto const now = Time::SteadyNow();
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
      return;
    }

    if (type.starts_with("crcp_")) {
      sm_->contact->ProcessCrcpMessage(peer_id, message);
      return;
    }

    auto peer = data_transport_->GetPeer(peer_id);
    auto contact = sm_->contact_manager->GetContact(peer_id);

    if (type == "peer_unwilling") {
      EventQueue::Instance().Push(EventContactUnwilling{peer_id, true});
      return;
    }

    bool const is_rejected = (contact && contact->status == identity::ContactStatus::rejected);
    bool const is_confirmed = (contact && contact->status == identity::ContactStatus::confirmed);

    if (!is_confirmed && type != "call_reject" && type != "hangup") {
      LGE << "CRCP Enforcement: Dropped message from unconfirmed or unverified peer";
      if (type == "call_request") {
        nlohmann::json const reject_msg = {
            {"type", "call_reject"},
            {"call_id", message.value("call_id", "")},
            {"reason", "unauthorized"}
        };
        std::ignore = data_transport_->SendJson(peer_id, reject_msg);
      } else if (is_rejected) {
        nlohmann::json const reject_msg = {{"type", "peer_unwilling"}};
        std::ignore = data_transport_->SendJson(peer_id, reject_msg);
      }
      return;
    }

    if (!peer || !peer->crcp_verified) {
      LGE << "CRCP Enforcement: Dropped message from unconfirmed or unverified peer";
      return;
    }

    if (type != "call_reject" && type != "hangup") {
      if (!contact->signature_from_them) {
        LGE << "CRCP Enforcement: No signature stored for peer: " << peer_id.ToString();
        return;
      }

      auto const sig = *contact->signature_from_them;
      if (!crypto::Identity::Verify(peer_id.Span(), sm_->account->GetId().Span(), sig)) {
        LGE << "CRCP Enforcement: Cryptographic validation failed for peer: " << peer_id.ToString();
        return;
      }
    }

    if (type == "hangup") {
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
      CheckPeerVersion(
          peer_id,
          message.value("sync_version", currentSyncVersion),
          message.value("ft_version", currentFileTransferVersion)
      );
      sm_->sync_engine->ProcessSyncMessage(peer_id, message);
    } else if (type == "chat" || type == "profile_info" || type == "profile_request" || type == "message_read" ||
               type == "message_receipt" || type == "file_cancel" || type == "message_delete" ||
               type == "message_edit" || type == "message_reaction") {
      // important to enforce a version mismatch notification on old apps which don't support protocol version check
      CheckPeerVersion(peer_id, 0, 0);
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

    peer->crcp_verified = false;
    peer->session_cipher->Reset();
    peer->last_activity_time = Time::SteadyNow();

    {
      std::lock_guard<std::mutex> const lock(unsuspend_mutex_);
      last_unsuspend_times_[peer_id] = Time::SteadyNow();
    }

    auto my_pub_key = peer->session_cipher->GetPublicKey();
    auto signature = sm_->account->Sign(peer_id.Span());
    Json const key_msg = {
        {"type", "key_exchange"},
        {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
        {"signature", crypto::ToHex(signature)},
        {"is_reply", false},
        {"sync_version", currentSyncVersion},
        {"ft_version", currentFileTransferVersion}
    };
    std::ignore = data_transport_->SendJson(peer_id, key_msg);
  }

 private:
  void HandleSdp(ContactID const& peer_id, std::string const& type, Json const& message) {
    std::string const context_str =
        message.value(transport::protocol::keys::context, transport::protocol::values::contextData);
    bool const is_media = (context_str == transport::protocol::values::contextMedia);

    if (!is_media) {
      LGD << "[Media Negotiation] Ignoring non-media SDP " << type << " from " << peer_id.ToString();
      return;  // We no longer negotiate DataChannels via SDP, libp2p handles data connections natively
    }
    LGI << "[Media Negotiation] Received media SDP " << type << " from " << peer_id.ToString();

    if (auto peer = data_transport_->GetPeer(peer_id); !peer) {
      if (type == transport::protocol::values::offer) {
        peer = std::make_shared<transport::Peer>(peer_id);
        data_transport_->AddPeer(peer_id, peer);
      } else {
        return;
      }
    } else {
      peer->last_activity_time = Time::SteadyNow();
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
    int const peer_ft_version = message.value("ft_version", 0);
    CheckPeerVersion(peer_id, peer_sync_version, peer_ft_version);

    if (auto const pub_key = message["key"].get<std::vector<std::byte>>();
        peer->session_cipher->ComputeSharedSecret(pub_key)) {
      auto const sig_hex = message.value("signature", "");
      auto sig = crypto::FromHex(sig_hex);
      if (!sig.empty() && crypto::Identity::Verify(peer_id.Span(), sm_->account->GetId().Span(), sig)) {
        peer->crcp_verified = true;
      }

      if (!message.value("is_reply", false)) {
        auto my_key = peer->session_cipher->GetPublicKey();
        auto const signature = sm_->account->Sign(peer_id.Span());
        data_transport_->SendJson(
            peer_id,
            {{"type", "key_exchange"},
             {"key", std::vector<std::byte>(my_key.begin(), my_key.end())},
             {"signature", crypto::ToHex(signature)},
             {"is_reply", true},
             {"sync_version", currentSyncVersion},
             {"ft_version", currentFileTransferVersion}}
        );
      }
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      LGE << "Failed key exchange with " << peer_id.ToString();
    }
  }

  // Important! Any breaking change of the sync protocol requires an increment of this constant.
  static constexpr int currentSyncVersion = 2;

  // Important! Any breaking change of the file transfer protocol requires an increment of this constant.
  static constexpr int currentFileTransferVersion = 1;

  void CheckPeerVersion(ContactID const& peer_id, int peer_sync_version, int peer_ft_version) {
    if (peer_sync_version < currentSyncVersion || peer_ft_version < currentFileTransferVersion) {
      EventQueue::Instance().Push(EventPeerVersionMismatch{peer_id, false});
      SendLegacyUpdatePrompt(peer_id);
    } else if (peer_sync_version > currentSyncVersion || peer_ft_version > currentFileTransferVersion) {
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
