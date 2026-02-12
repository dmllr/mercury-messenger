#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>
#include <span>

#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/call_id.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::services {

class InternalMessagingService {
 public:
  using Json = nlohmann::json;
  using IceConfigCallback = std::function<void(Json const&)>;

  InternalMessagingService(
      std::shared_ptr<transport::DataTransport> data_transport,
      std::shared_ptr<transport::MediaTransport> media_transport,
      std::shared_ptr<AccountService> account_service,
      std::shared_ptr<ContactService> contact_service,
      std::shared_ptr<MessagingService> messaging_service,
      std::shared_ptr<MediaCallService> media_call_service,
      std::shared_ptr<FileTransferService> file_transfer_service,
      std::shared_ptr<ProfileService> profile_service
  )
      : data_transport_(std::move(data_transport)),
        media_transport_(std::move(media_transport)),
        account_service_(std::move(account_service)),
        contact_service_(std::move(contact_service)),
        messaging_service_(std::move(messaging_service)),
        media_call_service_(std::move(media_call_service)),
        file_transfer_service_(std::move(file_transfer_service)),
        profile_service_(std::move(profile_service)) {
  }

  void SetServerIceConfigCallback(IceConfigCallback cb) {
    ice_config_callback_ = std::move(cb);
  }

  void ProcessSystemMessage(std::string const& type, Json const& message) {
    if (type == "auth_challenge") {
      auto const challenge = message.value("challenge", "");
      if (!challenge.empty()) {
        std::span<std::byte const> const challenge_bytes(
            reinterpret_cast<std::byte const*>(challenge.data()),
            challenge.size()
        );
        auto const signature = account_service_->Sign(challenge_bytes);
        Json const response = {
            {"type", "auth_response"},
            {"id", account_service_->GetId().ToString()},
            {"signature", crypto::ToHex(signature)}
        };
        data_transport_->SendSignalingMessage(response);
      }
    } else if (type == "login_success") {
      messaging_service_->StartDeliveryService();
    } else if (type == "ice_servers") {
      if (message.contains("servers") && ice_config_callback_) {
        ice_config_callback_(message["servers"]);
      }
    } else {
      LGW << "Received unknown system message of type: " << type;
    }
  }

  void ProcessSignalingMessage(ContactID const& peer_id, std::string const& type, Json const& message) {
    // SDP Signaling (Unified)
    if (type == "offer" || type == "answer") {
      HandleSdp(peer_id, type, message);
      return;
    }

    if (type == "contact_add_request") {
      std::optional<std::string> token;
      if (message.contains("token")) {
        token = message["token"];
      }
      Json const profile = message.value("profile", Json::object());
      contact_service_->OnContactAddRequest(peer_id, token, profile);
    } else if (type == "contact_confirm") {
      contact_service_->OnContactConfirm(peer_id);
    }
  }

  void SetupPeerHandlers(std::shared_ptr<transport::Peer> const& peer, ContactID const& peer_id) {
    peer->on_data_channel_message = [this, peer_id](std::span<std::byte const> msg) {
      if (auto p = data_transport_->GetPeer(peer_id)) {
        p->last_activity_time = Time::Now();
      }

      bool processed = false;
      if (!msg.empty() && msg[0] == std::byte{0xFE}) {
        file_transfer_service_->ProcessBinaryMessage(peer_id, msg);
        processed = true;
      } else {
        try {
          std::string json_str;
          json_str.resize(msg.size());
          std::memcpy(json_str.data(), msg.data(), msg.size());
          ProcessDataChannelMessage(peer_id, Json::parse(json_str));
          processed = true;
        } catch (...) {
          LGE << "Failed to parse data channel message from " << peer_id.ToString();
        }
      }

      if (processed) {
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
          messaging_service_->UnsuspendAndDeliver(peer_id);
        }
      }
    };
  }

  void ProcessDataChannelMessage(ContactID const& peer_id, Json const& message) {
    if (!message.contains("type")) return;
    std::string const type = message["type"];

    if (type == "key_exchange") {
      HandleKeyExchange(peer_id, message);
    } else if (type == "chat") {
      HandleChatMessage(peer_id, message);
    } else if (type == "message_receipt") {
      messaging_service_->OnMessageReceipt(peer_id, message["message_id"], message["status"]);
    } else if (type == "message_read") {
      messaging_service_->OnMessagesRead(peer_id, message["message_ids"].get<std::vector<uint64_t>>());
    } else if (type == "hangup") {
      media_call_service_->OnHangupSignal(peer_id, message.value("call_id", ""));
    } else if (type == "call_request") {
      auto kind = static_cast<messaging::HistoryEntryKind>(message.value("kind", 0));
      media_call_service_->OnCallRequest(peer_id, message.value("call_id", ""), kind);
    } else if (type == "call_reject") {
      media_call_service_->OnCallReject(peer_id, message.value("call_id", ""));
    } else if (type == "call_accept") {
      media_call_service_->OnCallAccepted(peer_id, message.value("call_id", ""));
    } else if (type == "call_ringing") {
      media_call_service_->OnCallRinging(peer_id, message.value("call_id", ""));
    } else if (type == "offer" || type == "answer") {
      // Media Signaling over Data Channel
      ProcessSignalingMessage(peer_id, type, message);
    } else if (type == "contact_removed") {
      contact_service_->OnContactRemoved(peer_id);
    } else if (type == "reconfirm_request") {
      contact_service_->OnReconfirmRequest(peer_id);
    } else if (type == "reconfirm_response") {
      contact_service_->OnReconfirmResponse(peer_id, message["signature"]);
    } else if (type == "contact_signature") {
      contact_service_->OnContactSignature(peer_id, message["signature"]);
    } else if (type == "profile_info") {
      profile_service_->OnProfileUpdate(peer_id, message);
    } else if (type == "profile_request") {
      profile_service_->SyncProfile(peer_id);
    } else if (type == "termination") {
      data_transport_->RemovePeer(peer_id);
    } else if (type == "file_cancel") {
      std::string const file_id = message.value("file_id", "");
      if (!file_id.empty()) {
        file_transfer_service_->HandleCancel(peer_id, file_id);
      }
    }
  }

  void OnDataChannelOpen(ContactID const& peer_id) {
    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      return;
    }

    LGI << "Data channel with " << peer_id.ToString() << " opened.";

    peer->session_cipher->Reset();
    peer->data_channel_is_open = true;
    peer->last_activity_time = Time::Now();

    {
      std::lock_guard<std::mutex> const lock(unsuspend_mutex_);
      last_unsuspend_times_[peer_id] = Time::Now();
    }

    if (account_service_->GetId() > peer_id) {
      auto my_pub_key = peer->session_cipher->GetPublicKey();
      Json const key_msg = {
          {"type", "key_exchange"},
          {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
          {"is_reply", false}
      };
      data_transport_->SendJson(peer_id, key_msg);
    }

    messaging_service_->UnsuspendAndDeliver(peer_id);
  }

 private:
  void HandleSdp(ContactID const& peer_id, std::string const& type, Json const& message) {
    std::string context_str = message.value("context", "data");
    bool is_media = (context_str == "media");

    auto peer = data_transport_->GetPeer(peer_id);
    if (!peer) {
      if (type == "offer") {
        peer = std::make_shared<transport::Peer>(peer_id);
        data_transport_->AddPeer(peer_id, peer);
        SetupPeerHandlers(peer, peer_id);
        media_transport_->AttachToPeer(peer_id, peer);
      } else {
        // Generally this shouldn't happen.
        return;
      }
    } else if (!peer->on_data_channel_message) {
      SetupPeerHandlers(peer, peer_id);
    }

    if (type == "offer") {
      if (is_media) {
        auto const sdp = message["sdp"].get<std::string>();
        media_call_service_->OnMediaOffer(peer_id, sdp);
      } else {
        // Data offer
        data_transport_->Connect(peer_id);
        data_transport_->SetRemoteDescription(peer_id, message["sdp"].get<std::string>(), "offer");
        data_transport_->CreateAnswer(peer_id);
      }
    } else if (type == "answer" || type == "pranswer") {
      if (is_media) {
        media_call_service_->OnMediaAnswer(peer_id, message["sdp"].get<std::string>());
      } else {
        data_transport_->SetRemoteDescription(peer_id, message["sdp"].get<std::string>(), "answer");
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
      messaging_service_->SendQueuedMessages(peer_id);
    } else {
      LGE << "Failed key exchange with " << peer_id.ToString();
    }
  }

  void HandleChatMessage(ContactID const& peer_id, Json const& message) {
    auto const msg_id = message["message_id"].get<uint64_t>();
    bool const is_sys = message.value("is_system", false);
    auto const payload = message["payload"].get<std::vector<std::byte>>();

    std::vector<messaging::Attachment> attachments;
    if (message.contains("attachments")) {
      for (auto const& j_att : message["attachments"]) {
        messaging::Attachment att;
        att.id = j_att.at("id").get<std::string>();
        att.type = j_att.at("type").get<messaging::AttachmentType>();
        att.mime_type = j_att.value("mime", "");
        att.size_bytes = j_att.value("size", 0ULL);
        att.thumbnail_base64 = j_att.value("thumb", "");
        att.hash = j_att.value("hash", "");
        att.note = j_att.value("note", "");
        att.file_name = j_att.value("name", "");
        att.is_downloaded = false;
        att.status = messaging::AttachmentStatus::pending;
        attachments.push_back(att);
      }
    }

    messaging_service_->OnChatMessageReceived(peer_id, payload, msg_id, is_sys, attachments);
  }

  std::shared_ptr<transport::DataTransport> data_transport_;
  std::shared_ptr<transport::MediaTransport> media_transport_;
  std::shared_ptr<AccountService> account_service_;
  std::shared_ptr<ContactService> contact_service_;
  std::shared_ptr<MessagingService> messaging_service_;
  std::shared_ptr<MediaCallService> media_call_service_;
  std::shared_ptr<FileTransferService> file_transfer_service_;
  std::shared_ptr<ProfileService> profile_service_;

  IceConfigCallback ice_config_callback_;

  std::unordered_map<ContactID, Time::Unit> last_unsuspend_times_;
  std::mutex unsuspend_mutex_;
  static constexpr Time::Unit kUnsuspendInterval = 5 * Time::uS;
};

}  // namespace eillance::services
