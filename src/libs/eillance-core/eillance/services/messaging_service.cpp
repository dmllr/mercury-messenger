#include "eillance/services/messaging_service.hpp"

#include <stdexcept>

#include "eillance/logging/logging.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

MessagingService::MessagingService(
    std::shared_ptr<storage::PersistentStorage> storage,
    std::shared_ptr<transport::Transport> transport,
    std::shared_ptr<identity::ContactManager> contact_manager,
    ConnectionRequestHandler connection_handler
)
    : storage_(std::move(storage)),
      transport_(std::move(transport)),
      contact_manager_(std::move(contact_manager)),
      connection_handler_(std::move(connection_handler)) {
  history_ = std::make_unique<messaging::MessageHistory>(storage_);

  // Register internal status change listener to propagate to the public callback
  history_->SetStatusChangedCallback(
      [this](ContactID const& peer_id, uint64_t msg_id, messaging::MessageStatus status) {
        INVARIANT(on_status_changed_);
        on_status_changed_(peer_id, msg_id, status);
      }
  );

  delivery_service_ =
      std::make_unique<messaging::MessageDeliveryService>(storage_, contact_manager_, [this](ContactID const& peer_id) {
        RequestConnection(peer_id);
      });
}

void MessagingService::StartDeliveryService() { delivery_service_->Start(); }

void MessagingService::StopDeliveryService() { delivery_service_->Stop(); }

void MessagingService::RequestConnection(ContactID const& peer_id) {
  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    // Delegate to Client to establish connection and setup handlers correctly
    connection_handler_(peer_id);
  }
}

void MessagingService::UpdatePeerActivity(ContactID const& peer_id) {
  if (auto peer = transport_->GetPeer(peer_id)) {
    peer->last_activity_time = Time::Now();
  }
}

auto MessagingService::SendChatMessage(ContactID const& peer_id, std::string const& message)
    -> messaging::HistoryEntryRecord {
  if (!contact_manager_->GetContact(peer_id)) {
    LGE << "Cannot send message to " << peer_id.ToString() << ". Not a contact.";
    return {};
  }

  auto entry =
      history_
          ->AddTextEntry(peer_id, message, messaging::MessageDirection::sent, messaging::HistoryEntryKind::textMessage);

  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->AttemptDelivery(peer_id);
  }

  return entry;
}

void MessagingService::SendSystemMessage(ContactID const& peer_id, std::string const& content) {
  history_
      ->AddTextEntry(peer_id, content, messaging::MessageDirection::sent, messaging::HistoryEntryKind::systemMessage);

  if (auto peer = transport_->GetPeer(peer_id); peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
    SendQueuedMessages(peer_id);
  } else {
    delivery_service_->AttemptDelivery(peer_id);
  }
}

void MessagingService::SendQueuedMessages(ContactID const& peer_id) {
  auto const messages = storage_->GetMessagesWithStatus(peer_id, messaging::MessageStatus::sending);
  if (messages.empty()) {
    delivery_service_->ConnectionAttemptFinished(peer_id);
    return;
  }

  auto const peer = transport_->GetPeer(peer_id);
  // Peer object might be missing if we are sending initial contact request signaling
  // so we check peer existence inside the loop for data channel messages.

  LGI << "Processing " << messages.size() << " queued message(s) for " << peer_id.ToString();

  for (auto const& entry : messages) {
    bool sent = false;

    if (entry.kind != messaging::HistoryEntryKind::textMessage &&
        entry.kind != messaging::HistoryEntryKind::systemMessage) {
      continue;
    }

    if (!std::holds_alternative<messaging::MessageEntry>(entry.data)) {
      continue;
    }

    std::string content_to_send;
    auto const& msg_entry = std::get<messaging::MessageEntry>(entry.data);

    if (entry.is_truncated) {
      if (auto full = history_->GetFullMessageContent(entry.id)) {
        content_to_send = *full;
      } else {
        continue;
      }
    } else {
      content_to_send = msg_entry.content;
    }

    // Special handling for System Messages that must be sent via Signaling (not Data Channel)
    if (entry.kind == messaging::HistoryEntryKind::systemMessage) {
      try {
        nlohmann::json content_json = nlohmann::json::parse(content_to_send);
        std::string const action = content_json.value("action", "");

        if (action == "contact_request" || action == "contact_confirm") {
          nlohmann::json msg = {
              {"type", action == "contact_request" ? "contact_add_request" : "contact_confirm"},
              {"to", peer_id.ToString()}
          };
          if (action == "contact_request") {
            if (content_json.contains("profile")) {
              msg["profile"] = content_json["profile"];
            }
            if (content_json.contains("token")) {
              msg["token"] = content_json["token"];
            }
          }
          transport_->SendSignalingMessage(msg);
          sent = true;
        }
      } catch (...) {
        // Ignore parse errors, treat as regular message
      }
    }

    // If not sent via signaling, try Data Channel
    if (!sent && peer && peer->data_channel_is_open && peer->session_cipher->IsReady()) {
      auto encrypted = peer->session_cipher->Encrypt(content_to_send);
      UpdatePeerActivity(peer_id);

      if (!encrypted.empty()) {
        nlohmann::json const json_msg = {
            {"type", "chat"},
            {"message_id", entry.id},
            {"is_system", (entry.kind == messaging::HistoryEntryKind::systemMessage)},
            {"payload", std::vector<std::byte>(encrypted.begin(), encrypted.end())}
        };
        transport_->SendPeerDataChannelMessage(peer_id, json_msg);
        sent = true;
      }
    }

    if (sent) {
      history_->UpdateMessageStatus(peer_id, entry.id, messaging::MessageStatus::sent);
    }
  }

  delivery_service_->ConnectionAttemptFinished(peer_id);
}

void MessagingService::OnConnectionAttemptFinished(ContactID const& peer_id) {
  delivery_service_->ConnectionAttemptFinished(peer_id);
}

// --- Queries ---

auto MessagingService::GetHistoryPage(ContactID const& peer_id, size_t page_index, size_t page_size) const
    -> messaging::HistoryPage {
  return history_->GetHistoryPage(peer_id, page_index, page_size);
}

auto MessagingService::GetFullMessageContent(uint64_t message_id) const -> std::optional<std::string> {
  return history_->GetFullMessageContent(message_id);
}

auto MessagingService::GetChatSummaries(std::vector<ContactID> const& peer_ids
) const -> std::vector<messaging::ChatSummary> {
  return history_->GetChatSummaries(peer_ids);
}

// --- Updates ---

void MessagingService::MarkMessagesAsRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) {
  history_->MarkMessagesAsRead(peer_id, message_ids);

  auto peer = transport_->GetPeer(peer_id);
  if (peer && peer->data_channel_is_open) {
    nlohmann::json const msg = {
        {"type", "message_read"},
        {"message_ids", message_ids},
    };
    transport_->SendPeerDataChannelMessage(peer_id, msg);
  }
}

void MessagingService::AddSystemEntry(
    ContactID const& peer_id,
    std::string const& content,
    messaging::MessageDirection direction
) {
  history_->AddTextEntry(peer_id, content, direction, messaging::HistoryEntryKind::systemMessage);
}

void MessagingService::AddCallEntry(
    ContactID const& peer_id,
    messaging::HistoryEntryKind kind,
    messaging::CallDirection direction,
    messaging::CallStatus status,
    uint32_t duration
) {
  history_->AddCallEntry(peer_id, kind, direction, status, duration);
}

// --- Incoming Events ---

void MessagingService::OnChatMessageReceived(
    ContactID const& peer_id,
    std::vector<std::byte> const& encrypted_payload,
    uint64_t message_id,
    bool is_system
) {
  auto peer = transport_->GetPeer(peer_id);
  if (!peer || !peer->session_cipher->IsReady()) {
    return;
  }

  UpdatePeerActivity(peer_id);

  auto decrypted = peer->session_cipher->Decrypt({encrypted_payload.begin(), encrypted_payload.end()});
  if (decrypted.empty()) {
    return;
  }

  auto entry = history_->AddTextEntry(
      peer_id,
      decrypted,
      messaging::MessageDirection::received,
      is_system ? messaging::HistoryEntryKind::systemMessage : messaging::HistoryEntryKind::textMessage
  );

  nlohmann::json const receipt = {{"type", "message_receipt"}, {"status", "delivered"}, {"message_id", message_id}};
  transport_->SendPeerDataChannelMessage(peer_id, receipt);

  INVARIANT(on_message_received_);
  on_message_received_(peer_id, entry);
}

void MessagingService::OnMessageReceipt(ContactID const& peer_id, uint64_t message_id, std::string const& status) {
  auto s = messaging::MessageStatus::sent;
  if (status == "delivered") {
    s = messaging::MessageStatus::delivered;
  } else if (status == "read") {
    s = messaging::MessageStatus::read;
  }

  history_->UpdateMessageStatus(peer_id, message_id, s);
}

void MessagingService::OnMessagesRead(ContactID const& peer_id, std::vector<uint64_t> const& message_ids) {
  for (auto id : message_ids) {
    history_->UpdateMessageStatus(peer_id, id, messaging::MessageStatus::read);
  }
}

void MessagingService::SetChatMessageReceivedCallback(OnChatMessageCallback cb) {
  on_message_received_ = std::move(cb);
}

void MessagingService::SetMessageStatusChangedCallback(OnMessageStatusChangedCallback cb) {
  on_status_changed_ = std::move(cb);
}

}  // namespace eillance::services
