#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/logging/logging.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/file_transfer_service.hpp"
#include "eillance/services/media_call_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/transport/transport.hpp"

namespace eillance::services {

class InternalMessagingService {
 public:
  using Json = nlohmann::json;

  InternalMessagingService(
      std::shared_ptr<transport::Transport> transport,
      std::shared_ptr<AccountService> account_service,
      std::shared_ptr<ContactService> contact_service,
      std::shared_ptr<MessagingService> messaging_service,
      std::shared_ptr<MediaCallService> media_call_service,
      std::shared_ptr<FileTransferService> file_transfer_service
  )
      : transport_(std::move(transport)),
        account_service_(std::move(account_service)),
        contact_service_(std::move(contact_service)),
        messaging_service_(std::move(messaging_service)),
        media_call_service_(std::move(media_call_service)),
        file_transfer_service_(std::move(file_transfer_service)) {}

  // Handles messages from the Signaling Server (Authentication, System status)
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
        transport_->SendSignalingMessage(response);
      }
    } else if (type == "login_success") {
      messaging_service_->StartDeliveryService();
    } else {
      LGW << "Received unknown system message of type: " << type;
    }
  }

  // Handles Signaling messages routed from a Peer (Contact requests, SDP, ICE)
  void ProcessSignalingMessage(ContactID const& peer_id, std::string const& type, Json const& message) {
    auto const context = message.value("context", "data");

    // Media Signaling
    if (context == "media") {
      HandleMediaSignaling(peer_id, type, message);
      return;
    }

    // Data Signaling (SDP/ICE)
    if (type == "offer" || type == "answer" || type == "candidate") {
      HandleDataSignaling(peer_id, type, message);
      return;
    }

    // Contact & Profile Signaling
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

  // Handles messages arriving via the WebRTC Data Channel (Chat, Key Exchange, Profile Sync)
  void ProcessDataChannelMessage(ContactID const& peer_id, Json const& message) {
    if (!message.contains("type")) return;
    std::string const type = message["type"];

    // Key Exchange (E2EE Handshake)
    if (type == "key_exchange") {
      HandleKeyExchange(peer_id, message);
      return;
    }

    // Chat
    if (type == "chat") {
      HandleChatMessage(peer_id, message);
      return;
    }

    // Receipts / Read Status
    if (type == "message_receipt") {
      messaging_service_->OnMessageReceipt(peer_id, message["message_id"], message["status"]);
    } else if (type == "message_read") {
      messaging_service_->OnMessagesRead(peer_id, message["message_ids"].get<std::vector<uint64_t>>());
    }

    // Calls
    else if (type == "hangup") {
      media_call_service_->Hangup(peer_id, false);
    }

    // Contact / Identity
    else if (type == "contact_removed") {
      contact_service_->OnContactRemoved(peer_id);
    } else if (type == "reconfirm_request") {
      contact_service_->OnReconfirmRequest(peer_id);
    } else if (type == "reconfirm_response") {
      contact_service_->OnReconfirmResponse(peer_id, message["signature"]);
    } else if (type == "contact_signature") {
      contact_service_->OnContactSignature(peer_id, message["signature"]);
    } else if (type == "profile_info") {
      contact_service_->OnProfileUpdate(peer_id, message);
    } else if (type == "profile_request") {
      contact_service_->SyncProfile(peer_id);
    } else if (type == "profile") {
      contact_service_->OnDeviceProfileReceived(peer_id, message["profile"]);
    } else if (type == "termination") {
      transport_->RemovePeer(peer_id);
    }

    // File Transfer
    else if (type == "file_chunk" || type == "file_cancel" || type == "file_resume_request") {
      file_transfer_service_->ProcessMessage(peer_id, message);
    }
  }

  void OnDataChannelOpen(ContactID const& peer_id) {
    auto peer = transport_->GetPeer(peer_id);
    if (!peer) return;

    LGI << "Data channel with " << peer_id.ToString() << " opened.";

    // Reset crypto session
    peer->session_cipher->Reset();
    peer->data_channel_is_open = true;
    peer->last_activity_time = Time::Now();

    // Initiator sends key first.
    // NOTE: In current architecture, ID comparison determines initiator for key exchange to avoid collision.
    if (account_service_->GetId() > peer_id) {
      auto my_pub_key = peer->session_cipher->GetPublicKey();
      Json const key_msg = {
          {"type", "key_exchange"},
          {"key", std::vector<std::byte>(my_pub_key.begin(), my_pub_key.end())},
          {"is_reply", false}
      };
      transport_->SendPeerDataChannelMessage(peer_id, key_msg);
    }

    // Triggers
    messaging_service_->SendQueuedMessages(peer_id);
    contact_service_->SyncProfile(peer_id);
  }

  // --- Helpers for Peer Handler Setup ---
  void SetupPeerHandlers(std::shared_ptr<transport::Peer> const& peer, ContactID const& peer_id) {
    peer->on_data_channel_message = [this, peer_id](std::string const& msg) {
      try {
        // Update activity
        if (auto p = transport_->GetPeer(peer_id)) {
          p->last_activity_time = Time::Now();
        }
        ProcessDataChannelMessage(peer_id, Json::parse(msg));
      } catch (...) {
        LGE << "Failed to parse data channel message from " << peer_id.ToString();
      }
    };
  }

 private:
  void HandleMediaSignaling(ContactID const& peer_id, std::string const& type, Json const& message) {
    auto peer = transport_->GetPeer(peer_id);
    if (!peer) {
      peer = std::make_shared<transport::Peer>(peer_id);
      transport_->AddPeer(peer_id, peer);
    }

    if (type == "offer") {
      transport_->CreateMediaPeerConnection(peer_id);
      peer->has_media_peer_connection = true;

      auto const sdp = message["sdp"].get<std::string>();
      bool const is_video = sdp.find("m=video") != std::string::npos;
      media_call_service_->OnCallRequest(
          peer_id,
          is_video ? messaging::HistoryEntryKind::videoCall : messaging::HistoryEntryKind::voiceCall
      );
      transport_->SetRemoteDescription(peer_id, "media", sdp, "offer");

    } else if (type == "candidate") {
      auto const candidate_str = message["candidate"].get<std::string>();
      auto const mid = message["sdpMid"].get<std::string>();
      transport_->AddRemoteCandidate(peer_id, "media", candidate_str, mid);
    } else if (type == "answer") {
      auto const sdp = message["sdp"].get<std::string>();
      transport_->SetRemoteDescription(peer_id, "media", sdp, type);
    }
  }

  void HandleDataSignaling(ContactID const& peer_id, std::string const& type, Json const& message) {
    if (type == "offer") {
      auto peer = transport_->GetPeer(peer_id);
      if (peer) {
        // Glare resolution: if both sides offer, the one with larger ID wins the role of offerer.
        if (account_service_->GetId() > peer_id) {
          transport_->ClosePeerConnection(peer_id, "data");
          transport_->RemovePeer(peer_id);
          peer = nullptr;
        } else {
          // We are smaller ID, we yield (ignore our own attempt or accept theirs)
          return;
        }
      }

      if (!peer) {
        peer = std::make_shared<transport::Peer>(peer_id);
        transport_->AddPeer(peer_id, peer);
        SetupPeerHandlers(peer, peer_id);
        transport_->CreateDataPeerConnection(peer_id);

        auto const sdp = message["sdp"].get<std::string>();
        transport_->SetRemoteDescription(peer_id, "data", sdp, "offer");
        transport_->CreateAnswer(peer_id, "data");
      }
    } else if (type == "answer") {
      transport_->SetRemoteDescription(peer_id, "data", message["sdp"].get<std::string>(), "answer");
    } else if (type == "candidate") {
      transport_->AddRemoteCandidate(
          peer_id,
          "data",
          message["candidate"].get<std::string>(),
          message["sdpMid"].get<std::string>()
      );
    }
  }

  void HandleKeyExchange(ContactID const& peer_id, Json const& message) {
    auto const peer = transport_->GetPeer(peer_id);
    if (!peer) return;

    auto pub_key = message["key"].get<std::vector<std::byte>>();
    if (peer->session_cipher->ComputeSharedSecret(pub_key)) {
      if (!message.value("is_reply", false)) {
        auto my_key = peer->session_cipher->GetPublicKey();
        transport_->SendPeerDataChannelMessage(
            peer_id,
            {{"type", "key_exchange"}, {"key", std::vector<std::byte>(my_key.begin(), my_key.end())}, {"is_reply", true}
            }
        );
      }
      // Connection fully established logic
      messaging_service_->SendQueuedMessages(peer_id);
      contact_service_->SyncProfile(peer_id);
    } else {
      LGE << "Failed key exchange with " << peer_id.ToString();
    }
  }

  void HandleChatMessage(ContactID const& peer_id, Json const& message) {
    auto msg_id = message["message_id"].get<uint64_t>();
    bool const is_sys = message.value("is_system", false);
    auto payload = message["payload"].get<std::vector<std::byte>>();

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
        att.is_downloaded = false;  // Remote file
        att.status = messaging::AttachmentStatus::pending;
        attachments.push_back(att);
      }
    }

    messaging_service_->OnChatMessageReceived(peer_id, payload, msg_id, is_sys, attachments);
  }

  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<AccountService> account_service_;
  std::shared_ptr<ContactService> contact_service_;
  std::shared_ptr<MessagingService> messaging_service_;
  std::shared_ptr<MediaCallService> media_call_service_;
  std::shared_ptr<FileTransferService> file_transfer_service_;
};

}  // namespace eillance::services
