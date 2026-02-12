#include "mercury/bot/bot.hpp"

#include <thread>

#include "eillance/client/client.hpp"
#include "eillance/rtc/data_transport_impl_libdatachannel.hpp"
#include "eillance/rtc/media_transport_impl_libdatachannel.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"

namespace mercury::bot {

class Bot::Impl {
  friend class Bot;

  Options options_;
  std::shared_ptr<eillance::Client> client_;
  std::function<void(Message const&)> message_handler_;

 public:
  explicit Impl(Options options)
      : options_(std::move(options)) {
    InitializeClient();
    SetupCallbacks();
  }

  Impl(std::shared_ptr<eillance::Client> client, Options options)
      : options_(std::move(options)),
        client_(std::move(client)) {
    SetupCallbacks();
  }

  void InitializeClient() {
    auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(options_.identity_path / "mercury.db");
    auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));

    auto data_transport_impl = std::make_unique<eillance::rtc::RtcDataTransport>();
    auto media_transport_impl = std::make_unique<eillance::rtc::RtcMediaTransport>();

    auto data_transport = std::make_shared<eillance::transport::DataTransport>(std::move(data_transport_impl));
    auto media_transport = std::make_shared<eillance::transport::MediaTransport>(std::move(media_transport_impl));

    client_ = std::make_shared<eillance::Client>(storage, data_transport, media_transport);
  }

  void SetupCallbacks() {
    if (options_.auto_accept_contacts) {
      client_->GetContactService()->SetContactRequestCallback([this](eillance::ContactID const& peer_id) {
        // Automatically accept friend requests
        client_->ConfirmContact(peer_id);
      });
    }

    if (options_.auto_reject_calls) {
      client_->GetMediaCallService()->SetIncomingCallCallback(
          [this](eillance::ContactID const& peer_id, eillance::messaging::HistoryEntryKind /*call_type*/) {
            client_->RejectCall(peer_id);
          }
      );
    }

    client_->GetMessagingService()->SetChatMessageReceivedCallback(
        [this](eillance::ContactID const& peer_id, eillance::messaging::HistoryEntryRecord const& record) {
          if (!message_handler_) {
            return;
          }

          if (record.kind != eillance::messaging::HistoryEntryKind::textMessage) {
            return;
          }

          if (!std::holds_alternative<eillance::messaging::MessageEntry>(record.data)) {
            return;
          }

          auto const& [content, attachments] = std::get<eillance::messaging::MessageEntry>(record.data);

          // Resolve sender info
          User user;
          user.id = peer_id.ToString();
          if (auto contact = client_->GetStorage()->GetContact(peer_id)) {
            user.first_name = contact->display_name.value_or("Unknown");
            user.bio = contact->bio.value_or("");
          } else {
            user.first_name = "Unknown";
          }

          Message msg;
          msg.message_id = record.id;
          msg.date = record.timestamp;
          msg.text = content;
          msg.from = std::move(user);

          message_handler_(msg);
        }
    );
  }
};

Bot::Bot(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {
}

Bot::Bot(std::shared_ptr<eillance::Client> client, Options options)
    : impl_(std::make_unique<Impl>(std::move(client), std::move(options))) {
}

Bot::~Bot() {
  Stop();
}

void Bot::Start() const {
  if (!impl_->client_->IdentityExists()) {
    throw std::runtime_error("Identity not found. Bot requires a pre-existing identity.");
  }

  if (!impl_->options_.signaling_url.empty()) {
    impl_->client_->UpdateSignalingUrl(impl_->options_.signaling_url);
  }

  // The client starts its own threads.
  // We detach the thread running the client event loop.
  // The lifecycle is managed by the Bot class destructor calling Stop().
  std::thread([client = impl_->client_] { client->Start(); }).detach();
}

void Bot::Stop() const {
  if (impl_ && impl_->client_) {
    impl_->client_->Terminate();
  }
}

void Bot::SendMessage(std::string const& chat_id, std::string const& text) const {
  eillance::ContactID const peer_id(chat_id);
  std::ignore = impl_->client_->SendChatMessage(peer_id, text);
}

void Bot::OnMessage(std::function<void(Message const&)> handler) const {
  impl_->message_handler_ = std::move(handler);
}

auto Bot::GetId() const -> std::string {
  return impl_->client_->GetId().ToString();
}

}  // namespace mercury::bot
