#include "mercury/bot/bot.hpp"

#include <thread>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/rtc/media_transport_impl_libdatachannel.hpp"
#include "eillance/storage/sqlite/persistent_storage_impl_sqlite.hpp"
#include "eillance/utils/event_queue.hpp"

namespace mercury::bot {

class Bot::Impl {
  friend class Bot;

  Options options_;
  std::shared_ptr<eillance::Client> client_;
  std::function<void(Message const&)> message_handler_;
  std::vector<eillance::EventQueue::SubscriberTypeId> event_unsubs_;

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

  ~Impl() {
    for (auto const& evnt : event_unsubs_) {
      eillance::EventQueue::Instance().Unsubscribe(evnt);
    }
  }

  void InitializeClient() {
    auto storage_impl = std::make_unique<eillance::storage::SQLiteStorage>(options_.identity_path / "mercury.db");
    auto storage = std::make_shared<eillance::storage::PersistentStorage>(std::move(storage_impl));

    auto media_transport_impl = std::make_unique<eillance::rtc::RtcMediaTransport>();
    auto media_transport = std::make_shared<eillance::transport::MediaTransport>(std::move(media_transport_impl));

    client_ = std::make_shared<eillance::Client>(storage, media_transport, nullptr, eillance::transport::NodeRole::bot);
  }

  void SetupCallbacks() {
    if (options_.auto_accept_contacts) {
      event_unsubs_.emplace_back(
          eillance::EventQueue::Instance().Subscribe<eillance::EventContactRequest>([this](auto const& e) -> void {
            client_->ConfirmContact(e.peer_id);
          })
      );
    }

    if (options_.auto_reject_calls) {
      event_unsubs_.emplace_back(
          eillance::EventQueue::Instance().Subscribe<eillance::EventIncomingCall>([this](auto const& e) -> void {
            client_->RejectCall(e.peer_id);
          })
      );
    }

    event_unsubs_.emplace_back(
        eillance::EventQueue::Instance().Subscribe<eillance::EventChatMessageReceived>([this](auto const& e) -> void {
          if (!message_handler_) {
            return;
          }
          if (e.message.kind != eillance::messaging::HistoryEntryKind::textMessage) {
            return;
          }

          auto const* content = eillance::messaging::GetMessageContent(e.message.data);
          if (!content) {
            return;
          }

          User user;
          user.id = e.message.author_id.ToString();
          if (auto contact = client_->GetStorage()->GetContact(e.message.author_id)) {
            user.first_name = contact->display_name.value_or("Unknown");
            user.bio = contact->bio.value_or("");
          } else {
            user.first_name = "Unknown";
          }

          Message msg;
          msg.message_id = e.message.id;
          msg.date = e.message.timestamp;
          msg.text = content->content;
          msg.from = std::move(user);
          msg.chat_id = e.topic_id.ToString();

          message_handler_(msg);
        })
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

  // The client starts its own threads.
  // We detach the thread running the client event loop.
  // The lifecycle is managed by the Bot class destructor calling Stop().
  std::thread([client = impl_->client_]() -> void { client->Start(); }).detach();
}

void Bot::InitializeIdentity(ProfileConfig const& profile) const {
  if (impl_->client_->IdentityExists()) {
    return;
  }

  LGI << "Identity not found. Generating new identity...";
  std::string const mnemonic = impl_->client_->GenerateNewIdentity();
  LGI << "New identity generated. Mnemonic: " << mnemonic;

  UpdateProfile(profile);
}

void Bot::UpdateProfile(ProfileConfig const& profile) const {
  eillance::identity::ProfileUpdate update;
  if (profile.display_name) {
    update.display_name = profile.display_name;
  }
  if (profile.bio) {
    update.bio = profile.bio;
  }
  if (profile.photo) {
    update.photo_base64 = profile.photo;
  }
  impl_->client_->UpdateMyProfile(update);
  LGI << "Profile updated.";
}

void Bot::Stop() const {
  if (impl_ && impl_->client_) {
    impl_->client_->Terminate();
  }
}

void Bot::SendMessage(std::string const& chat_id, std::string const& text) const {
  eillance::TopicID const topic(chat_id);
  std::ignore = impl_->client_->SendChatMessage(topic, text);
}

void Bot::MarkAsRead(std::string const& chat_id, std::uint64_t message_id) const {
  eillance::TopicID const topic(chat_id);
  impl_->client_->MarkMessagesAsRead(topic, {message_id});
}

void Bot::OnMessage(std::function<void(Message const&)> handler) const {
  impl_->message_handler_ = std::move(handler);
}

auto Bot::GetId() const -> std::string {
  return impl_->client_->GetId().ToString();
}

}  // namespace mercury::bot
