#include "mercury/bot/bot.hpp"

#include "eillance/client/client.hpp"
#include "eillance/rtc/transport_impl_libdatachannel.hpp"
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

  void InitializeClient() {
    auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(options_.identity_path / "mercury.db");
    auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));
    auto rtc_transport_impl = std::make_unique<eillance::rtc::RtcTransport>();
    auto transport = std::make_shared<eillance::transport::Transport>(std::move(rtc_transport_impl));

    client_ = std::make_shared<eillance::Client>(storage, transport);
  }

  void SetupCallbacks() const {
    if (options_.auto_accept_contacts) {
      client_->SetContactRequestCallback([this](eillance::ContactID const& peer_id) {
        // Automatically accept friend requests
        client_->ConfirmContact(peer_id);
      });
    }

    if (options_.auto_reject_calls) {
      client_->SetIncomingCallCallback(
          [this](eillance::ContactID const& peer_id, eillance::messaging::HistoryEntryKind /*call_type*/) {
            client_->RejectCall(peer_id);
          }
      );
    }

    client_->SetChatMessageReceivedCallback(
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

          auto const& [content] = std::get<eillance::messaging::MessageEntry>(record.data);

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
    : impl_(std::make_unique<Impl>(std::move(options))) {}

Bot::~Bot() { Stop(); }

void Bot::Start() const {
  if (!impl_->client_->IdentityExists()) {
    throw std::runtime_error("Identity not found. Bot requires a pre-existing identity.");
  }

  if (!impl_->options_.signaling_url.empty()) {
    impl_->client_->UpdateSignalingUrl(impl_->options_.signaling_url);
  }

  // The client starts its own threads.
  // Note: Client::Start() assumes ownership or management of threads.
  // In a real production API we might want to manage the thread lifecycle more explicitly here,
  // but eillance::Client handles it internally via std::thread detached or managed.
  // Looking at Client::Start(), it spawns a thread.
  std::thread([c = impl_->client_] { c->Start(); }).detach();
}

void Bot::Stop() const {
  if (impl_ && impl_->client_) {
    impl_->client_->Terminate();
  }
}

void Bot::SendMessage(std::string const& chat_id, std::string const& text) const {
  eillance::ContactID const peer_id(chat_id);
  impl_->client_->SendChatMessage(peer_id, text);
}

void Bot::OnMessage(std::function<void(Message const&)> handler) const { impl_->message_handler_ = std::move(handler); }

auto Bot::GetId() const -> std::string { return impl_->client_->GetId().ToString(); }

}  // namespace mercury::bot
