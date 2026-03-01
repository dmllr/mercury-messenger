#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include <ranges>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/time.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ConversationViewModel {
 public:
  explicit ConversationViewModel(ContactID const& peer_id, std::shared_ptr<Client> client)
      : impl_(std::make_shared<Impl>(peer_id, std::move(client))) {
    impl_->StartInitialLoad();

    auto& queue = EventQueue::Instance();
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
    Subscribe<EventMessageUpdated>(queue, [impl](EventMessageUpdated const& event) {
      if (event.peer_id == impl->peer_id) {
        impl->OnMessageUpdated(event.message_id);
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
    Subscribe<EventDataReloaded>(queue, [impl](auto const&) { impl->StartInitialLoad(); });
  }

  ~ConversationViewModel() {
    for (auto const& unsubscribe : eventUnsubscribers_) {
      unsubscribe();
    }
  }

  [[nodiscard]] auto GetState() const -> ConversationState {
    return impl_->GetState();
  }

  void SendText(std::string const& content) const {
    impl_->SendText(content);
  }
  void SendMessage(std::string const& content, std::vector<messaging::AttachmentRequest> const& attachments) const {
    impl_->SendMessage(content, attachments);
  }
  void DeleteMessage(uint64_t message_id) const {
    impl_->client->Messaging()->SendDeleteMessage(impl_->peer_id, message_id);
  }
  void EditMessage(uint64_t message_id, std::string const& content) const {
    impl_->client->Messaging()->SendEditMessage(impl_->peer_id, message_id, content);
  }
  void ReactToMessage(uint64_t message_id, messaging::Reaction reaction) const {
    impl_->client->Messaging()->SendReaction(impl_->peer_id, message_id, reaction);
  }

  void LoadOlderMessages() const {
    impl_->LoadOlderMessages();
  }
  void LoadUpToMessage(uint64_t message_id) const {
    impl_->LoadUpToMessage(message_id);
  }
  void LoadFullMessage(uint64_t message_id) const {
    impl_->LoadFullMessage(message_id);
  }
  void MarkMessagesAsRead(std::vector<std::uint64_t> const& message_ids) const {
    impl_->MarkMessagesAsRead(message_ids);
  }
  void AddSystemMessage(std::string const& message) const {
    impl_->AddSystemMessage(message);
  }

  void CancelTransfer(std::string const& attachment_id, bool is_upload) const {
    impl_->client->CancelTransfer(impl_->peer_id, attachment_id, is_upload);
  }

  void ResumeTransfer(std::string const& attachment_id, bool is_upload) const {
    impl_->client->ResumeTransfer(impl_->peer_id, attachment_id, is_upload);
  }

  void DiscardAttachment(std::string const& attachment_id) const {
    impl_->client->DiscardAttachment(impl_->peer_id, attachment_id);
  }

 private:
  struct Impl : public std::enable_shared_from_this<Impl> {
    ContactID const peer_id;
    std::shared_ptr<Client> client;

    mutable std::mutex mutex;
    ConversationState state;
    std::size_t chat_pages_loaded = 0;
    std::atomic<int> load_generation{0};

    // Store pending status updates for attachments that might not be in history yet (race condition)
    std::unordered_map<std::string, messaging::AttachmentStatus> pending_attachment_updates;

    inline static std::atomic<std::uint64_t> nextSystemMsgId{10000000000000000000ULL};
    static constexpr size_t kPageSize = 50;

    Impl(ContactID peer_id, std::shared_ptr<Client> client)
        : peer_id(peer_id),
          client(std::move(client)) {
      state.peer_id = peer_id;
    }

    auto GetState() const -> ConversationState {
      std::lock_guard<std::mutex> const lock(mutex);
      return state;
    }

    void SendText(std::string const& content) {
      SendMessage(content, {});
    }

    void SendMessage(std::string const& content, std::vector<messaging::AttachmentRequest> const& attachments) {
      // Access MessagingService directly since facade might not have the overload yet
      auto entry = client->Messaging()->SendChatMessage(peer_id, content, attachments);

      std::lock_guard<std::mutex> const lock(mutex);

      // If we have pending updates for these attachments (e.g. upload finished synchronously), apply them now
      if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
        auto& msg_entry = std::get<messaging::MessageEntry>(entry.data);
        for (auto& att : msg_entry.attachments) {
          if (pending_attachment_updates.contains(att.id)) {
            att.status = pending_attachment_updates[att.id];
            pending_attachment_updates.erase(att.id);
          }
        }
      }

      state.history.push_back(entry);
      EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
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
        EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
      } else if (state.has_older_messages) {
        state.has_older_messages = false;
        EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
      }
    }

    void LoadUpToMessage(uint64_t message_id) {
      std::lock_guard<std::mutex> const lock(mutex);

      // Invalidate any pending initial load to prevent it from overwriting our work
      load_generation++;

      bool found = false;
      for (auto const& msg : state.history) {
        if (msg.id == message_id) {
          found = true;
          break;
        }
      }

      // If history is empty, reset pager to ensure we fetch from start
      if (state.history.empty()) {
        state.has_older_messages = true;
        chat_pages_loaded = 0;
      }

      while (!found && state.has_older_messages) {
        auto page = client->GetMessageHistoryPage(peer_id, chat_pages_loaded, kPageSize);
        if (page.messages.empty()) {
          state.has_older_messages = false;
          break;
        }
        for (auto& message : std::ranges::reverse_view(page.messages)) {
          state.history.push_front(message);
          if (message.id == message_id) {
            found = true;
          }
        }
        chat_pages_loaded++;
        state.has_older_messages = (chat_pages_loaded * kPageSize) < page.total_messages;
      }

      EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
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
      if (updated) {
        EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
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
           .timestamp = static_cast<Time::Unit>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                    std::chrono::system_clock::now().time_since_epoch()
           )
                                                    .count()),
           .direction = messaging::MessageDirection::received,
           .is_truncated = false,
           .data = messaging::MessageEntry{message}}
      );
      EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
    }

    void OnMessageReceived(ContactID const&, messaging::HistoryEntryRecord const& message) {
      std::lock_guard<std::mutex> const lock(mutex);
      state.history.push_back(message);
      EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
    }

    void OnMessageStatusChanged(ContactID const&, uint64_t const message_id, messaging::MessageStatus const status) {
      std::lock_guard<std::mutex> const lock(mutex);

      // If status is None (0), it means the message was dismissed/deleted. Remove it from the list.
      if (status == messaging::MessageStatus::none) {
        std::erase_if(state.history, [message_id](auto const& e) { return e.id == message_id; });
        EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
        return;
      }

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
      if (changed) EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
    }

    void OnMessageUpdated(uint64_t const message_id) {
      auto const updated_entry = client->GetHistoryEntry(peer_id, message_id);
      if (!updated_entry) {
        return;
      }

      std::lock_guard<std::mutex> const lock(mutex);
      for (auto& entry : state.history) {
        if (entry.id == message_id) {
          entry = *updated_entry;

          // Manually check attachments because DB fetch might have stale attachment status (pending)
          // while VM knows current transfer state.
          if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
            auto& msg_entry = std::get<messaging::MessageEntry>(entry.data);
            for (auto& att : msg_entry.attachments) {
              if (pending_attachment_updates.contains(att.id)) {
                att.status = pending_attachment_updates[att.id];
              }
            }
          }
          break;
        }
      }
      EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
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

      if (found) EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
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

      if (found) {
        EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});

        // If we found it, remove from pending updates just in case it was stuck there
        pending_attachment_updates.erase(attachment_id);
      } else {
        // Not found, buffer it. This handles the case where OnAttachmentStatusChanged fires (synchronously)
        // inside SendMessage before the entry is pushed to history.
        pending_attachment_updates[attachment_id] = status;
      }
    }

    void OnTransferProgress(std::string const& attachment_id, float const progress) const {
      EventQueue::Instance().Push(EventTransferProgressUpdated{peer_id, attachment_id, progress});
    }

    void LoadInitialHistory(std::shared_ptr<Impl> const&, int const generation) {
      auto [messages, total_messages] = client->GetMessageHistoryPage(peer_id, 0, kPageSize);
      std::lock_guard<std::mutex> const lock(mutex);
      if (generation != load_generation) return;

      state.history.assign(messages.begin(), messages.end());
      chat_pages_loaded = 1;
      state.has_older_messages = (kPageSize < total_messages);

      // Cleanup pending updates if any match loaded history (unlikely but safe)
      for (auto& entry : state.history) {
        if (std::holds_alternative<messaging::MessageEntry>(entry.data)) {
          auto& msg = std::get<messaging::MessageEntry>(entry.data);
          for (auto& att : msg.attachments) {
            if (pending_attachment_updates.contains(att.id)) {
              att.status = pending_attachment_updates[att.id];
              pending_attachment_updates.erase(att.id);
            }
          }
        }
      }

      EventQueue::Instance().Push(EventConversationStateUpdated{peer_id, state});
    }

    void StartInitialLoad() {
      int const gen = ++load_generation;
      std::thread([this, self = shared_from_this(), gen] { LoadInitialHistory(self, gen); }).detach();
    }
  };

  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<std::function<void()>> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
    auto id = queue.Subscribe<EventType>(std::move(callback));
    eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
  }
};

}  // namespace eillance::view_model
