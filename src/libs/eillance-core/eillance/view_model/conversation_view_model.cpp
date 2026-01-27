#include "conversation_view_model.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>

#include <ranges>

#include "eillance/logging/logging.hpp"
#include "eillance/utils/time.hpp"

namespace eillance::view_model {

struct ConversationViewModel::Impl : public std::enable_shared_from_this<Impl> {
  ContactID const peer_id;
  std::shared_ptr<Client> client;
  std::shared_ptr<ClientEventRouter> router;
  std::shared_ptr<Observer> observer;

  mutable std::mutex mutex;
  State state;
  std::size_t chat_pages_loaded = 0;
  std::atomic<int> load_generation{0};

  inline static std::atomic<std::uint64_t> nextSystemMsgId{10000000000000000000ULL};
  static constexpr size_t kPageSize = 50;

  Impl(ContactID peer_id, std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : peer_id(peer_id),
        client(std::move(client)),
        router(std::move(router)) {
    state.peer_id = peer_id;
  }

  void SetObserver(std::shared_ptr<Observer> obs) {
    std::lock_guard<std::mutex> const lock(mutex);
    observer = std::move(obs);
    if (observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  auto GetState() const -> State {
    std::lock_guard<std::mutex> const lock(mutex);
    return state;
  }

  void SendText(std::string const& content) { SendMessage(content, {}); }

  void SendMessage(std::string const& content, std::vector<services::AttachmentRequest> const& attachments) {
    // Access MessagingService directly since facade might not have the overload yet
    auto entry = client->GetMessagingService()->SendChatMessage(peer_id, content, attachments);
    {
      std::lock_guard<std::mutex> const lock(mutex);
      state.history.push_back(entry);
      if (observer) {
        observer->OnHistoryUpdated(state);
      }
    }
  }

  void LoadOlderMessages() {
    std::lock_guard<std::mutex> const lock(mutex);
    auto page = client->GetMessageHistoryPage(peer_id, chat_pages_loaded, kPageSize);

    if (!page.messages.empty()) {
      for (auto& message : std::ranges::reverse_view(page.messages)) {
        state.history.push_front(message);
      }
      chat_pages_loaded++;
      state.has_older_messages = (chat_pages_loaded * kPageSize) < page.total_messages;
      if (observer) {
        observer->OnHistoryUpdated(state);
      }
    } else if (state.has_older_messages) {
      state.has_older_messages = false;
      if (observer) {
        observer->OnHistoryUpdated(state);
      }
    }
  }

  void LoadFullMessage(uint64_t message_id) {
    auto full_content = client->GetFullMessageContent(peer_id, message_id);
    if (!full_content) return;

    std::lock_guard<std::mutex> const lock(mutex);
    bool updated = false;
    for (auto& entry : state.history) {
      if (entry.id == message_id) {
        if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
          auto& msg_entry = std::get<messaging::MessageEntry>(entry.data);
          msg_entry.content = *full_content;
          entry.is_truncated = false;
          updated = true;
          break;
        }
      }
    }
    if (updated && observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  void MarkMessagesAsRead(std::vector<std::uint64_t> const& message_ids) {
    if (!message_ids.empty()) {
      client->MarkMessagesAsRead(peer_id, message_ids);
    }
  }

  void AddSystemMessage(std::string const& message) {
    std::lock_guard<std::mutex> const lock(mutex);
    state.history.push_back(
        {.id = nextSystemMsgId++,
         .kind = messaging::HistoryEntryKind::systemMessage,
         .timestamp = static_cast<Time::Unit>(
             std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch())
                 .count()
         ),
         .direction = messaging::MessageDirection::received,
         .is_truncated = false,
         .data = messaging::MessageEntry{message}}
    );
    if (observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  void OnMessageReceived(ContactID const&, messaging::HistoryEntryRecord const& message) {
    std::lock_guard<std::mutex> const lock(mutex);
    state.history.push_back(message);
    if (observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  void OnMessageStatusChanged(ContactID const&, uint64_t const message_id, messaging::MessageStatus const status) {
    std::lock_guard<std::mutex> const lock(mutex);
    bool changed = false;
    for (auto& entry : state.history) {
      if (entry.id == message_id) {
        if (entry.status != status) {
          entry.status = status;
          changed = true;
        }
        break;
      }
    }
    if (changed && observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  void OnAttachmentDownloaded(std::string const& attachment_id) {
    std::lock_guard<std::mutex> const lock(mutex);
    bool found = false;

    for (auto& entry : state.history) {
      if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
        auto& msg = std::get<messaging::MessageEntry>(entry.data);
        for (auto& att : msg.attachments) {
          if (att.id == attachment_id) {
            att.is_downloaded = true;  // Actual state update
            att.status = messaging::AttachmentStatus::done;
            found = true;
          }
        }
      }
    }

    if (found && observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  void OnAttachmentStatusChanged(std::string const& attachment_id, messaging::AttachmentStatus status) {
    std::lock_guard<std::mutex> const lock(mutex);
    bool found = false;

    for (auto& entry : state.history) {
      if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
        auto& msg = std::get<messaging::MessageEntry>(entry.data);
        for (auto& att : msg.attachments) {
          if (att.id == attachment_id) {
            att.status = status;
            found = true;
          }
        }
      }
    }

    if (found && observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  void OnTransferProgress(std::string const& attachment_id, float const progress) const {
    // Forward progress to observer directly
    std::lock_guard<std::mutex> const lock(mutex);
    if (observer) {
      observer->OnTransferProgress(attachment_id, progress);
    }
  }

  void LoadInitialHistory(std::shared_ptr<Impl> const&, int const generation) {
    auto [messages, total_messages] = client->GetMessageHistoryPage(peer_id, 0, kPageSize);
    std::lock_guard<std::mutex> const lock(mutex);
    if (generation != load_generation) return;

    state.history.assign(messages.begin(), messages.end());
    chat_pages_loaded = 1;
    state.has_older_messages = (kPageSize < total_messages);
    if (observer) {
      observer->OnHistoryUpdated(state);
    }
  }

  void StartInitialLoad() {
    int const gen = ++load_generation;
    std::thread([this, self = shared_from_this(), gen] { LoadInitialHistory(self, gen); }).detach();
  }
};

ConversationViewModel::ConversationViewModel(
    ContactID const& peer_id,
    std::shared_ptr<Client> client,
    std::shared_ptr<ClientEventRouter> router
)
    : impl_(std::make_shared<Impl>(peer_id, std::move(client), std::move(router))) {
  impl_->StartInitialLoad();

  auto& queue = impl_->router->Queue();
  auto impl = impl_;
  Subscribe<EventChatMessageReceived>(queue, [impl](EventChatMessageReceived const& event) {
    if (event.peer_id == impl->peer_id) {
      impl->OnMessageReceived(event.peer_id, event.message);
    }
  });
  Subscribe<EventMessageStatusChanged>(queue, [impl](EventMessageStatusChanged const& event) {
    if (event.peer_id == impl->peer_id) {
      impl->OnMessageStatusChanged(event.peer_id, event.message_id, event.status);
    }
  });
  Subscribe<EventAttachmentDownloaded>(queue, [impl](EventAttachmentDownloaded const& event) {
    if (event.peer_id == impl->peer_id) {
      impl->OnAttachmentDownloaded(event.attachment_id);
    }
  });
  Subscribe<EventTransferProgress>(queue, [impl](EventTransferProgress const& event) {
    if (event.peer_id == impl->peer_id) {
      impl->OnTransferProgress(event.attachment_id, event.progress);
    }
  });
  Subscribe<EventAttachmentStatusChanged>(queue, [impl](EventAttachmentStatusChanged const& event) {
    if (event.peer_id == impl->peer_id) {
      impl->OnAttachmentStatusChanged(event.attachment_id, event.status);
    }
  });
  Subscribe<EventDebugDataReset>(queue, [impl](auto const&) { impl->StartInitialLoad(); });
}

ConversationViewModel::~ConversationViewModel() {
  for (auto const& unsubscribe : eventUnsubscribers_) {
    unsubscribe();
  }
}

template <typename EventType>
void ConversationViewModel::Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void ConversationViewModel::SetObserver(std::shared_ptr<Observer> observer) const {
  impl_->SetObserver(std::move(observer));
}

auto ConversationViewModel::GetState() const -> State { return impl_->GetState(); }
void ConversationViewModel::SendText(std::string const& content) const { impl_->SendText(content); }
void ConversationViewModel::SendMessage(
    std::string const& content,
    std::vector<services::AttachmentRequest> const& attachments
) const {
  impl_->SendMessage(content, attachments);
}
void ConversationViewModel::LoadOlderMessages() const { impl_->LoadOlderMessages(); }
void ConversationViewModel::LoadFullMessage(uint64_t const message_id) const { impl_->LoadFullMessage(message_id); }
void ConversationViewModel::MarkMessagesAsRead(std::vector<std::uint64_t> const& message_ids) const {
  impl_->MarkMessagesAsRead(message_ids);
}
void ConversationViewModel::AddSystemMessage(std::string const& message) const { impl_->AddSystemMessage(message); }

void ConversationViewModel::CancelTransfer(std::string const& attachment_id, bool is_upload) const {
  impl_->client->CancelTransfer(impl_->peer_id, attachment_id, is_upload);
}

void ConversationViewModel::ResumeTransfer(std::string const& attachment_id, bool is_upload) const {
  impl_->client->ResumeTransfer(impl_->peer_id, attachment_id, is_upload);
}

}  // namespace eillance::view_model
