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

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventPeerMessage>([this](EventPeerMessage const& e) {
      auto const from_it = e.message.find("from");
      if (from_it == e.message.end()) return;
      ContactID const peer_id(from_it->get<std::string>());
      auto const type_it = e.message.find("type");
      if (type_it == e.message.end()) return;
      this->ProcessSignalingMessage(peer_id, type_it->get<std::string>(), e.message);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventSystemMessage>([this](EventSystemMessage const& e) {
      this->ProcessSystemMessage(e.type, e.message);
    }));

    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventDataChannelOpen>([this](auto const& e) {
      this->OnDataChannelOpen(e.peer_id);
    }));
  }

  ~InternalMessagingService() {
    for (auto const id : event_unsubs_) EventQueue::Instance().Unsubscribe(id);
  }

  void ProcessSystemMessage(std::string const& type, Json const& message) {
    if (type == transport::protocol::values::authChallenge) {
      auto const challenge = message.value(transport::protocol::keys::challenge, "");
      if (!challenge.empty()) {
        std::span<std::byte const> const challenge_bytes(
            reinterpret_cast<std::byte const*>(challenge.data()),
            challenge.size()
        );
        auto const signature = sm_->account->Sign(challenge_bytes);
        auto const response = transport::protocol::CreateAuthResponse(sm_->account->GetId(), crypto::ToHex(signature));
        data_transport_->SendSignalingMessage(response);
      }
    } else if (type == transport::protocol::values::loginSuccess) {
      EventQueue::Instance().Push(EventServiceConnectionStateChanged{ServiceConnectionState::online});
    } else if (type == transport::protocol::values::iceServers) {
      if (message.contains(transport::protocol::keys::servers)) {
        EventQueue::Instance().Push(EventServerIceConfig{message[transport::protocol::keys::servers].dump()});
      }
    } else {
      LGW << "Received unknown system message of type: " << type;
    }
  }

  void ProcessSignalingMessage(ContactID const& peer_id, std::string const& type, Json const& message) {
    if (type == transport::protocol::values::offer || type == transport::protocol::values::answer) {
      HandleSdp(peer_id, type, message);
      return;
    } else if (type == "sync_propose" || type == "sync_provide" || type == "sync_confirm") {
      sm_->sync_engine->ProcessSyncMessage(peer_id, message);
    }
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
      if (now - last_unsuspend_times_[peer_id] >= kUnsuspendInterval) {
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
      ProcessSignalingMessage(peer_id, type, message);
    } else if (type == "termination") {
      data_transport_->RemovePeer(peer_id);
    } else if (type == "sync_propose" || type == "sync_provide" || type == "sync_confirm") {
      sm_->sync_engine->ProcessSyncMessage(peer_id, message);
    }
  }

  void OnDataChannelOpen(ContactID const& peer_id) {
    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      return;
    }

    if (peer->data_channel_is_open.exchange(true)) {
      LGI << "Data channel with " << peer_id.ToString() << " is already open. Ignoring duplicate event.";
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
          {"is_reply", false}
      };
      std::ignore = data_transport_->SendJson(peer_id, key_msg);
    }
  }

 private:
  void HandleSdp(ContactID const& peer_id, std::string const& type, Json const& message) {
    std::string const context_str =
        message.value(transport::protocol::keys::context, transport::protocol::values::contextData);
    bool const is_media = (context_str == transport::protocol::values::contextMedia);

    if (auto peer = data_transport_->GetPeer(peer_id); !peer) {
      if (type == transport::protocol::values::offer) {
        peer = std::make_shared<transport::Peer>(peer_id);
        data_transport_->AddPeer(peer_id, peer);
        media_transport_->AttachToPeer(peer_id, peer);
      } else {
        return;
      }
    } else {
      peer->last_activity_time = Time::Now();
    }

    if (type == transport::protocol::values::offer) {
      if (is_media) {
        auto const sdp = message[transport::protocol::keys::sdp].get<std::string>();
        sm_->media_call->OnMediaOffer(peer_id, sdp);
      } else {
        auto const state = data_transport_->GetConnectionState(peer_id);
        if (state == transport::PeerConnectionState::newConnection ||
            state == transport::PeerConnectionState::connecting) {
          if (sm_->account->GetId() > peer_id) {
            LGI << "Resolving SDP glare: Yielding to offer from " << peer_id.ToString();
            data_transport_->Disconnect(peer_id);
          } else {
            LGI << "Resolving SDP glare: Ignoring offer from " << peer_id.ToString();
            return;
          }
        } else {
          data_transport_->Disconnect(peer_id);
        }

        data_transport_->Connect(peer_id);
        data_transport_->SetRemoteDescription(
            peer_id,
            message[transport::protocol::keys::sdp].get<std::string>(),
            transport::protocol::values::offer
        );
        data_transport_->CreateAnswer(peer_id);
      }
    } else if (type == transport::protocol::values::answer || type == transport::protocol::values::prAnswer) {
      if (is_media) {
        sm_->media_call->OnMediaAnswer(peer_id, message[transport::protocol::keys::sdp].get<std::string>());
      } else {
        data_transport_->SetRemoteDescription(
            peer_id,
            message[transport::protocol::keys::sdp].get<std::string>(),
            transport::protocol::values::answer
        );
      }
    }
  }

  void HandleKeyExchange(ContactID const& peer_id, Json const& message) {
    auto const peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      return;
    }

    auto pub_key = message["key"].get<std::vector<std::byte>>();
    if (peer->session_cipher->ComputeSharedSecret(pub_key)) {
      if (!message.value("is_reply", false)) {
        auto my_key = peer->session_cipher->GetPublicKey();
        data_transport_->SendJson(
            peer_id,
            {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_key.begin(), my_key.end())}, {"is_reply", true}
            }
        );
      }
      EventQueue::Instance().Push(EventSendQueuedMessages{peer_id});
    } else {
      LGE << "Failed key exchange with " << peer_id.ToString();
    }
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
