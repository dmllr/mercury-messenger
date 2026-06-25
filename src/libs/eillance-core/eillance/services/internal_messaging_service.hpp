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

    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](EventDataChannelOpen const& e) -> void {
          this->OnDataChannelOpen(e.peer_id);  // e.peer_id is network_id
        })
    );

    event_unsubs_.push_back(
        EventQueue::Instance().Subscribe<EventSendMediaNegotiationMessage>([this](auto const& e) -> void {
          std::string const to_str = e.message.value("to", "");
          if (!to_str.empty()) {
            ContactID const peer_id(to_str);
            ContactID target_id = peer_id;
            if (auto net_id = sm_->media_call->GetActiveNetworkIdForCall(peer_id)) {
              target_id = *net_id;
            }
            if (auto p = data_transport_->GetPeer(target_id)) {
              p->pending_offer_sdp = "";  // Mark as sent to prevent forced fallback
            }
            auto contact = sm_->contact_manager->GetContact(peer_id);
            if (!contact || contact->status != identity::ContactStatus::confirmed) {
              return;
            }

            std::ignore = data_transport_->SendJson(target_id, e.message);
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

  void HandleDataChannelMessage(ContactID const& network_id, std::span<std::byte const> const msg) {
    if (auto peer = data_transport_->GetPeer(network_id)) {
      peer->last_activity_time = Time::SteadyNow();
    }

    ContactID const master_id = data_transport_->GetMasterId(network_id);

    if (msg.empty() || msg[0] != std::byte{0xFE}) {
      try {
        std::string json_str;
        json_str.resize(msg.size());
        std::memcpy(json_str.data(), msg.data(), msg.size());
        ProcessDataChannelMessage(master_id, network_id, Json::parse(json_str));
      } catch (...) {
        LGE << "Failed to parse data channel message from " << master_id.ToString();
      }
    }

    bool should_unsuspend = false;
    {
      auto const now = Time::SteadyNow();
      std::lock_guard<std::mutex> const lock(unsuspend_mutex_);
      auto const last = last_unsuspend_times_[master_id];
      if (now > last && (now - last) >= kUnsuspendInterval) {
        last_unsuspend_times_[master_id] = now;
        should_unsuspend = true;
      }
    }
    if (should_unsuspend) {
      EventQueue::Instance().Push(EventPeerActivity{master_id});
    }
  }

  void ProcessDataChannelMessage(ContactID const& master_id, ContactID const& network_id, Json const& message) {
    if (!message.contains("type")) {
      return;
    }

    std::string const type = message["type"];

    if (type == "key_exchange") {
      HandleKeyExchange(network_id, message);
      return;
    }

    if (type.starts_with("crcp_")) {
      sm_->contact->ProcessCrcpMessage(master_id, network_id, message);
      return;
    }

    auto peer = data_transport_->GetPeer(network_id);
    auto contact = sm_->contact_manager->GetContact(master_id);

    if (type == "peer_unwilling") {
      EventQueue::Instance().Push(EventContactUnwilling{master_id, true});
      return;
    }

    bool const is_self = (master_id == sm_->account->GetId());
    bool const is_rejected = (!is_self && contact && contact->status == identity::ContactStatus::rejected);
    bool const is_confirmed = (is_self || (contact && contact->status == identity::ContactStatus::confirmed));

    if (!is_confirmed && type != "call_reject" && type != "hangup") {
      LGE << "CRCP Enforcement: Dropped message from unconfirmed or unverified peer";
      sm_->sync_engine->AbortSync(master_id);
      if (type == "call_request") {
        Json const reject_msg = {
            {"type", "call_reject"},
            {"call_id", message.value("call_id", "")},
            {"reason", "unauthorized"}
        };
        std::ignore = data_transport_->SendJson(master_id, reject_msg);
      } else if (is_rejected) {
        Json const reject_msg = {{"type", "peer_unwilling"}};
        std::ignore = data_transport_->SendJson(master_id, reject_msg);
      }
      return;
    }

    if (!peer || !peer->crcp_verified) {
      LGE << "CRCP Enforcement: Dropped message from unconfirmed or unverified peer";
      sm_->sync_engine->AbortSync(master_id);
      return;
    }

    if (type != "call_reject" && type != "hangup" && !is_self) {
      if (!contact || !contact->signature_from_them) {
        LGE << "CRCP Enforcement: No signature stored for peer: " << master_id.ToString();
        sm_->sync_engine->AbortSync(master_id);
        return;
      }

      auto const sig = *contact->signature_from_them;
      if (!crypto::Identity::Verify(master_id.Span(), sm_->account->GetId().Span(), sig)) {
        LGE << "CRCP Enforcement: Cryptographic validation failed for peer: " << master_id.ToString();
        sm_->sync_engine->AbortSync(master_id);
        return;
      }
    }

    if (type == "hangup") {
      sm_->media_call->OnHangupSignal(master_id, message.value("call_id", ""));
    } else if (type == "call_request") {
      auto kind = static_cast<messaging::HistoryEntryKind>(message.value("kind", 0));
      sm_->media_call->OnCallRequest(master_id, network_id, message.value("call_id", ""), kind);
    } else if (type == "call_reject") {
      sm_->media_call->OnCallReject(master_id, message.value("call_id", ""));
    } else if (type == "call_accept") {
      sm_->media_call->OnCallAccepted(master_id, network_id, message.value("call_id", ""));
    } else if (type == "call_ringing") {
      sm_->media_call->OnCallRinging(master_id, message.value("call_id", ""));
    } else if (type == "call_answered_elsewhere") {
      sm_->media_call
          ->OnCallAnsweredElsewhere(master_id, message.value("call_id", ""), message.value("accepted_device", ""));
    } else if (type == transport::protocol::values::offer || type == transport::protocol::values::answer) {
      HandleSdp(master_id, type, message);
    } else if (type == "termination") {
      data_transport_->RemovePeer(network_id);
    } else if (type.starts_with("sync_")) {
      CheckPeerVersion(
          network_id,
          message.value("sync_version", currentSyncVersion),
          message.value("ft_version", currentFileTransferVersion)
      );
      sm_->sync_engine->ProcessSyncMessage(network_id, message);
    } else if (type == "chat" || type == "profile_info" || type == "profile_request" || type == "message_read" ||
               type == "message_receipt" || type == "file_cancel" || type == "message_delete" ||
               type == "message_edit" || type == "message_reaction") {
      // important to enforce a version mismatch notification on old apps which don't support protocol version check
      CheckPeerVersion(master_id, 0, 0);
    }
  }

  void OnDataChannelOpen(ContactID const& network_id) {
    auto peer = data_transport_->GetPeer(network_id);
    if (!peer) {
      return;
    }

    if (peer->data_channel_initialized.exchange(true)) {
      LGI << "Data channel with " << network_id.ToString() << " is already initialized. Ignoring duplicate event.";
      return;
    }

    LGI << "Data channel with " << network_id.ToString() << " opened.";

    peer->crcp_verified = false;
    peer->session_cipher->Reset();
    peer->last_activity_time = Time::SteadyNow();

    ContactID const master_id = data_transport_->GetMasterId(network_id);

    {
      std::lock_guard<std::mutex> const lock(unsuspend_mutex_);
      last_unsuspend_times_[master_id] = Time::SteadyNow();
    }

    auto my_pub_key = peer->session_cipher->GetPublicKey();
    auto my_device_id = sm_->account->GetDevicePublicKey();
    auto signature = sm_->account->Sign(my_device_id);
    Json const key_msg = {
        {"type", "key_exchange"},
        {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
        {"signature", crypto::ToHex(signature)},
        {"is_reply", false},
        {"m_id", sm_->account->GetId().ToString()},
        {"sync_version", currentSyncVersion},
        {"ft_version", currentFileTransferVersion}
    };
    std::ignore = data_transport_->SendJson(network_id, key_msg);
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

  void HandleKeyExchange(ContactID const& network_id, Json const& message) {
    auto const peer = data_transport_->GetPeer(network_id);
    if (!peer) {
      return;
    }

    if (peer->crcp_verified) {
      // Already verified. Ignore duplicate or reply key_exchanges to prevent cipher reset race condition
      return;
    }

    ContactID const master_id = message.contains("m_id") ? ContactID(message["m_id"].get<std::string>()) : network_id;

    int const peer_sync_version = message.value("sync_version", 0);
    int const peer_ft_version = message.value("ft_version", 0);
    CheckPeerVersion(master_id, peer_sync_version, peer_ft_version);

    if (auto const pub_key = message["key"].get<std::vector<std::byte>>();
        peer->session_cipher->ComputeSharedSecret(pub_key)) {
      auto const sig_hex = message.value("signature", "");
      auto sig = crypto::FromHex(sig_hex);

      auto my_device_id = ContactID(sm_->account->GetDevicePublicKey());
      auto my_master_id = sm_->account->GetId();

      bool verified = false;
      if (!sig.empty()) {
        // Primary (New secure behavior): Check if remote master signed remote device ID
        if (crypto::Identity::Verify(master_id.Span(), network_id.Span(), sig)) {
          verified = true;
        }
        // Fallback 1: Check if remote master signed our local device ID (backwards compatibility)
        else if (crypto::Identity::Verify(master_id.Span(), my_device_id.Span(), sig)) {
          verified = true;
        }
        // Fallback 2: Check if remote master signed our local master ID (backwards compatibility)
        else if (crypto::Identity::Verify(master_id.Span(), my_master_id.Span(), sig)) {
          verified = true;
        }
        // Fallback 3: Check if remote device signed our local device ID
        else if (crypto::Identity::Verify(network_id.Span(), my_device_id.Span(), sig)) {
          verified = true;
        }
      }

      if (verified) {
        peer->crcp_verified = true;
        data_transport_->LinkDeviceToMaster(network_id, master_id);
      } else {
        LGE << "CRCP verification failed for " << master_id.ToString() << " from network id " << network_id.ToString();
      }

      if (!message.value("is_reply", false)) {
        auto my_key = peer->session_cipher->GetPublicKey();
        auto my_device_id_vec = sm_->account->GetDevicePublicKey();
        auto const signature = sm_->account->Sign(my_device_id_vec);
        std::ignore = data_transport_->SendJson(
            network_id,
            {{"type", "key_exchange"},
             {"key", std::vector<std::byte>(my_key.begin(), my_key.end())},
             {"signature", crypto::ToHex(signature)},
             {"is_reply", true},
             {"m_id", sm_->account->GetId().ToString()},
             {"sync_version", currentSyncVersion},
             {"ft_version", currentFileTransferVersion}}
        );
      }
      if (verified) {
        EventQueue::Instance().Push(EventSendQueuedMessages{master_id});
        if (master_id == sm_->account->GetId()) {
          EventQueue::Instance().Push(EventSelfDeviceConnected{network_id});
        }
      }
    } else {
      LGE << "Failed key exchange with " << network_id.ToString();
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
    auto const peer = data_transport_->GetPeer(peer_id);
    if (!peer || !peer->session_cipher->IsReady()) {
      return;
    }
    if (peer->legacy_prompt_sent.exchange(true)) {
      return;
    }

    Json payload_json;
    payload_json["v"] = services::MessagingService::protocolVersion;
    payload_json["c"] = Json{{"action", "unsupported_format"}}.dump();

    auto encrypted = peer->session_cipher->Encrypt(payload_json.dump());
    if (encrypted.empty()) {
      return;
    }

    Json const json_msg = {
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
