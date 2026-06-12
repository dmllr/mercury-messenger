#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
  explicit ConversationViewModel(TopicID const& topic_id, std::shared_ptr<Client> client)
      : impl_(std::make_shared<Impl>(topic_id, std::move(client))) {
    impl_->StartInitialLoad();

    auto& queue = EventQueue::Instance();
    auto impl = impl_;
    Subscribe<EventChatMessageReceived>(queue, [impl](EventChatMessageReceived const& event) -> void {
      if (event.topic_id == impl->topic_id) {
        impl->OnMessageReceived(event.topic_id, event.message);
      }
    });
    Subscribe<EventMessageStatusChanged>(queue, [impl](EventMessageStatusChanged const& event) -> void {
      if (event.topic_id == impl->topic_id) {
        impl->OnMessageStatusChanged(event.topic_id, event.message_id, event.status);
      }
    });
    Subscribe<EventMessageUpdated>(queue, [impl](EventMessageUpdated const& event) -> void {
      if (event.topic_id == impl->topic_id) {
        impl->OnMessageUpdated(event.message_id);
      }
    });
    Subscribe<EventAttachmentDownloaded>(queue, [impl](EventAttachmentDownloaded const& event) -> void {
      if (impl->IsRelevantAttachmentEvent(event.topic_id, event.attachment_id)) {
        impl->OnAttachmentDownloaded(event.attachment_id);
      }
    });
    Subscribe<EventTransferProgress>(queue, [impl](EventTransferProgress const& event) -> void {
      if (impl->IsRelevantAttachmentEvent(event.topic_id, event.attachment_id)) {
        impl->OnTransferProgress(event.attachment_id, event.progress);
      }
    });
    Subscribe<EventAttachmentStatusChanged>(queue, [impl](EventAttachmentStatusChanged const& event) -> void {
      if (impl->IsRelevantAttachmentEvent(event.topic_id, event.attachment_id)) {
        impl->OnAttachmentStatusChanged(event.attachment_id, event.status);
      }
    });
    Subscribe<EventContactUnreachable>(queue, [impl](EventContactUnreachable const& event) -> void {
      if (event.peer_id == ContactID(impl->topic_id.Span())) {
        std::lock_guard<std::mutex> const lock(impl->mutex);
        if (impl->state.is_unreachable != event.is_unreachable) {
          impl->state.is_unreachable = event.is_unreachable;
          EventQueue::Instance().Push(EventConversationStateUpdated{impl->topic_id, impl->state});
        }
      }
    });
    Subscribe<EventContactUnwilling>(queue, [impl](EventContactUnwilling const& event) -> void {
      if (event.peer_id == ContactID(impl->topic_id.Span())) {
        std::lock_guard<std::mutex> const lock(impl->mutex);
        if (impl->state.is_unwilling != event.is_unwilling) {
          impl->state.is_unwilling = event.is_unwilling;
          EventQueue::Instance().Push(EventConversationStateUpdated{impl->topic_id, impl->state});
        }
      }
    });
    Subscribe<EventSyncStateChanged>(queue, [impl](EventSyncStateChanged const& event) -> void {
      if (event.peer_id == ContactID(impl->topic_id.Span())) {
        std::lock_guard<std::mutex> const lock(impl->mutex);
        if (impl->state.is_syncing != event.is_syncing || impl->state.sync_progress != event.progress) {
          impl->state.is_syncing = event.is_syncing;
          impl->state.sync_progress = event.progress;
          EventQueue::Instance().Push(EventConversationStateUpdated{impl->topic_id, impl->state});
        }
      }
    });
    Subscribe<EventDataPeerConnectionStateChanged>(queue, [impl](auto const& event) -> void {
      if (event.peer_id == ContactID(impl->topic_id.Span())) {
        if (event.state == transport::PeerConnectionState::disconnected ||
            event.state == transport::PeerConnectionState::closed ||
            event.state == transport::PeerConnectionState::failed) {
          std::lock_guard<std::mutex> const lock(impl->mutex);
          if (impl->state.is_syncing) {
            impl->state.is_syncing = false;
            EventQueue::Instance().Push(EventConversationStateUpdated{impl->topic_id, impl->state});
          }
        }
      }
    });
    Subscribe<EventDataReloaded>(queue, [impl](auto const&) -> auto { impl->StartInitialLoad(); });
  }

  ~ConversationViewModel() {
    for (auto const id : eventUnsubscribers_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

  [[nodiscard]] auto GetState() const -> ConversationState {
    return impl_->GetState();
  }

  void SendText(std::string const& content) const {
    impl_->SendText(content);
  }
  void SendMessage(std::string const& content, uint64_t reply_to_id = 0) const {
    impl_->SendMessage(content, reply_to_id);
  }
  void AddDraftAttachment(DraftAttachment const& draft) const {
    impl_->AddDraftAttachment(draft);
  }
  void RemoveDraftAttachment(int index) const {
    impl_->RemoveDraftAttachment(index);
  }
  void UpdateDraftNote(int index, std::string const& note) const {
    impl_->UpdateDraftNote(index, note);
  }
  void OnMessageShown(uint64_t message_id) const {
    impl_->OnMessageShown(message_id);
  }
  void DeleteMessage(uint64_t message_id) const {
    impl_->client->Messaging()->SendDeleteMessage(impl_->topic_id, message_id);
  }
  void EditMessage(uint64_t message_id, std::string const& content) const {
    impl_->client->Messaging()->SendEditMessage(impl_->topic_id, message_id, content);
  }
  void ReactToMessage(uint64_t message_id, messaging::Reaction reaction) const {
    impl_->client->Messaging()->SendReaction(impl_->topic_id, message_id, reaction);
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
    impl_->client->CancelTransfer(impl_->topic_id, attachment_id, is_upload);
  }

  void RetrySync() const {
    impl_->client->RetrySync(impl_->topic_id);
  }

  void ResumeTransfer(std::string const& attachment_id, bool is_upload) const {
    impl_->client->ResumeTransfer(impl_->topic_id, attachment_id, is_upload);
  }

  void DiscardAttachment(std::string const& attachment_id) const {
    impl_->client->DiscardAttachment(impl_->topic_id, attachment_id);
  }

  [[nodiscard]] auto GetHistoryEntry(uint64_t message_id) const -> std::optional<messaging::HistoryEntryRecord> {
    return impl_->client->GetHistoryEntry(impl_->topic_id, message_id);
  }

  [[nodiscard]] auto GetDownloadedAttachments() const -> std::vector<messaging::AttachmentRecord> {
    return impl_->client->GetDownloadedAttachments(impl_->topic_id);
  }

  void DeleteAttachmentLocal(std::string const& attachment_id) const {
    impl_->client->DeleteAttachmentLocal(impl_->topic_id, attachment_id);
  }

 private:
  struct Impl : public std::enable_shared_from_this<Impl> {
    TopicID const topic_id;
    std::shared_ptr<Client> client;

    mutable std::mutex mutex;
    ConversationState state;
    std::size_t chat_pages_loaded = 0;
    std::atomic<int> load_generation{0};

    std::unordered_set<uint64_t> read_queue;
    bool read_debounce_active = false;

    // Store pending status updates for attachments that might not be in history yet (race condition)
    std::unordered_map<std::string, messaging::AttachmentStatus> pending_attachment_updates;

    static constexpr std::uint64_t kMinSystemMessageId = 10000000000000000000ULL;
    inline static std::atomic<std::uint64_t> nextSystemMsgId{kMinSystemMessageId};
    static constexpr size_t kPageSize = 50;

    Impl(TopicID topic_id, std::shared_ptr<Client> client)
        : topic_id(topic_id),
          client(std::move(client)) {
      state.topic_id = topic_id;
      state.is_unreachable = this->client->IsUnreachable(topic_id);
    }

    bool IsRelevantAttachmentEvent(TopicID const& event_topic_id, std::string const& attachment_id) const {
      if (event_topic_id == topic_id) return true;
      std::lock_guard<std::mutex> const lock(mutex);
      if (pending_attachment_updates.contains(attachment_id)) return true;
      for (auto const& entry : state.history) {
        if (auto const* content = messaging::GetMessageContent(entry.data)) {
          for (auto const& att : content->attachments) {
            if (att.id == attachment_id) return true;
          }
        }
      }
      return false;
    }

    auto GetState() const -> ConversationState {
      std::lock_guard<std::mutex> const lock(mutex);
      return state;
    }

    void AddDraftAttachment(DraftAttachment const& draft) {
      std::lock_guard<std::mutex> const lock(mutex);
      state.drafts.push_back(draft);
      EventQueue::Instance().Push(EventConversationDraftsUpdated{topic_id, state.drafts});
    }

    void RemoveDraftAttachment(int index) {
      std::lock_guard<std::mutex> const lock(mutex);
      if (index >= 0 && index < state.drafts.size()) {
        state.drafts.erase(state.drafts.begin() + index);
        EventQueue::Instance().Push(EventConversationDraftsUpdated{topic_id, state.drafts});
      }
    }

    void UpdateDraftNote(int index, std::string const& note) {
      std::lock_guard<std::mutex> const lock(mutex);
      if (index >= 0 && index < state.drafts.size()) {
        state.drafts[index].note = note;
        EventQueue::Instance().Push(EventConversationDraftsUpdated{topic_id, state.drafts});
      }
    }

    void SendText(std::string const& content) {
      SendMessage(content, 0);
    }

    void SendMessage(std::string const& content, uint64_t reply_to_id) {
      std::vector<messaging::AttachmentRequest> reqs;
      {
        std::lock_guard<std::mutex> const lock(mutex);
        if (content.empty() && state.drafts.empty()) return;
        for (auto const& d : state.drafts) {
          reqs.push_back({d.file_path, d.type, d.thumbnail_base64, d.mime_type, d.note, d.file_name});
        }
        state.drafts.clear();
        EventQueue::Instance().Push(EventConversationDraftsUpdated{topic_id, state.drafts});
      }

      // Access MessagingService directly since facade might not have the overload yet
      auto entry = client->SendChatMessage(topic_id, content, reqs, reply_to_id);

      std::lock_guard<std::mutex> const lock(mutex);

      // If we have pending updates for these attachments (e.g. upload finished synchronously), apply them now
      if (auto* content = messaging::GetMessageContent(entry.data)) {
        for (auto& att : content->attachments) {
          if (pending_attachment_updates.contains(att.id)) {
            att.status = pending_attachment_updates[att.id];
            pending_attachment_updates.erase(att.id);
          }
        }
      }

      state.history.push_back(entry);
      EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void LoadOlderMessages() {
      std::lock_guard<std::mutex> const lock(mutex);
      auto page = client->GetMessageHistoryPage(topic_id, chat_pages_loaded, kPageSize);

      if (!page.messages.empty()) {
        for (auto& message : std::ranges::reverse_view(page.messages)) {
          bool exists = false;
          for (auto const& entry : state.history) {
            if (entry.id == message.id) {
              exists = true;
              break;
            }
          }
          if (!exists) {
            state.history.push_front(message);
          }
        }
        chat_pages_loaded++;
        state.has_older_messages = (chat_pages_loaded * kPageSize) < page.total_messages;
        EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
      } else if (state.has_older_messages) {
        state.has_older_messages = false;
        EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
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
        auto page = client->GetMessageHistoryPage(topic_id, chat_pages_loaded, kPageSize);
        if (page.messages.empty()) {
          state.has_older_messages = false;
          break;
        }
        for (auto& message : std::ranges::reverse_view(page.messages)) {
          bool exists = false;
          for (auto const& entry : state.history) {
            if (entry.id == message.id) {
              exists = true;
              break;
            }
          }
          if (!exists) {
            state.history.push_front(message);
          }
          if (message.id == message_id) {
            found = true;
          }
        }
        chat_pages_loaded++;
        state.has_older_messages = (chat_pages_loaded * kPageSize) < page.total_messages;
      }

      EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void LoadFullMessage(uint64_t message_id) {
      auto full_content = client->GetFullMessageContent(topic_id, message_id);
      if (!full_content) return;

      std::lock_guard<std::mutex> const lock(mutex);
      bool updated = false;
      for (auto& entry : state.history) {
        if (entry.id == message_id) {
          if (auto* content = messaging::GetMessageContent(entry.data)) {
            content->content = *full_content;
            entry.is_truncated = false;
            updated = true;
            break;
          }
        }
      }
      if (updated) {
        EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
      }
    }

    void MarkMessagesAsRead(std::vector<std::uint64_t> const& message_ids) {
      if (!message_ids.empty()) {
        client->MarkMessagesAsRead(topic_id, message_ids);
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
           .data = messaging::ChatMessageEntry{messaging::MessageContent{message, {}, 0}}}
      );
      EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void OnMessageReceived(TopicID const&, messaging::HistoryEntryRecord const& message) {
      std::lock_guard<std::mutex> const lock(mutex);
      for (auto const& entry : state.history) {
        if (entry.id == message.id) {
          return;
        }
      }

      auto msg_copy = message;
      if (auto* content = messaging::GetMessageContent(msg_copy.data)) {
        for (auto& att : content->attachments) {
          if (pending_attachment_updates.contains(att.id)) {
            att.status = pending_attachment_updates[att.id];
            pending_attachment_updates.erase(att.id);
          }
        }
      }
      state.history.push_back(msg_copy);
      EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void OnMessageStatusChanged(TopicID const&, uint64_t const message_id, messaging::MessageStatus const status) {
      std::lock_guard<std::mutex> const lock(mutex);

      // If status is None (0), it means the message was dismissed/deleted. Remove it from the list.
      if (status == messaging::MessageStatus::none) {
        std::erase_if(state.history, [message_id](auto const& e) { return e.id == message_id; });
        EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
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
      if (changed) EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void OnMessageUpdated(uint64_t const message_id) {
      auto const updated_entry = client->GetHistoryEntry(topic_id, message_id);
      if (!updated_entry) {
        return;
      }

      std::lock_guard<std::mutex> const lock(mutex);
      for (auto& entry : state.history) {
        if (entry.id == message_id) {
          entry = *updated_entry;

          // Manually check attachments because DB fetch might have stale attachment status (pending)
          // while VM knows current transfer state.
          if (auto* content = messaging::GetMessageContent(entry.data)) {
            for (auto& att : content->attachments) {
              if (pending_attachment_updates.contains(att.id)) {
                att.status = pending_attachment_updates[att.id];
              }
            }
          }
          break;
        }
      }
      EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void OnAttachmentDownloaded(std::string const& attachment_id) {
      std::lock_guard<std::mutex> const lock(mutex);
      bool found = false;

      for (auto& entry : state.history) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto& att : content->attachments) {
            if (att.id == attachment_id) {
              att.is_downloaded = true;  // Actual state update
              att.status = messaging::AttachmentStatus::done;
              found = true;
            }
          }
        }
      }

      if (found) EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void OnMessageShown(uint64_t message_id) {
      if (message_id >= kMinSystemMessageId) return;
      std::lock_guard<std::mutex> const lock(mutex);
      read_queue.insert(message_id);
      if (!read_debounce_active) {
        read_debounce_active = true;
        std::thread([this, self = shared_from_this()] {
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
          std::vector<uint64_t> ids;
          {
            std::lock_guard<std::mutex> const lock(mutex);
            ids.assign(read_queue.begin(), read_queue.end());
            read_queue.clear();
            read_debounce_active = false;
          }
          if (!ids.empty()) {
            client->MarkMessagesAsRead(topic_id, ids);
          }
        }).detach();
      }
    }

    void OnAttachmentStatusChanged(std::string const& attachment_id, messaging::AttachmentStatus status) {
      std::lock_guard<std::mutex> const lock(mutex);
      bool found = false;

      for (auto& entry : state.history) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto& att : content->attachments) {
            if (att.id == attachment_id) {
              att.status = status;
              found = true;
            }
          }
        }
      }

      if (found) {
        EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});

        // If we found it, remove from pending updates just in case it was stuck there
        pending_attachment_updates.erase(attachment_id);
      } else {
        // Not found, buffer it. This handles the case where OnAttachmentStatusChanged fires (synchronously)
        // inside SendMessage before the entry is pushed to history.
        pending_attachment_updates[attachment_id] = status;
      }
    }

    void OnTransferProgress(std::string const& attachment_id, float const progress) const {
      EventQueue::Instance().Push(EventTransferProgressUpdated{topic_id, attachment_id, progress});
    }

    void LoadInitialHistory(std::shared_ptr<Impl> const&, int const generation) {
      auto [messages, total_messages] = client->GetMessageHistoryPage(topic_id, 0, kPageSize);
      std::lock_guard<std::mutex> const lock(mutex);
      if (generation != load_generation) return;

      state.history.assign(messages.begin(), messages.end());
      chat_pages_loaded = 1;
      state.has_older_messages = (kPageSize < total_messages);

      // Cleanup pending updates if any match loaded history (unlikely but safe)
      for (auto& entry : state.history) {
        if (auto* content = messaging::GetMessageContent(entry.data)) {
          for (auto& att : content->attachments) {
            if (pending_attachment_updates.contains(att.id)) {
              att.status = pending_attachment_updates[att.id];
              pending_attachment_updates.erase(att.id);
            }
          }
        }
      }

      EventQueue::Instance().Push(EventConversationStateUpdated{topic_id, state});
    }

    void StartInitialLoad() {
      int const gen = ++load_generation;
      std::thread([this, self = shared_from_this(), gen] { LoadInitialHistory(self, gen); }).detach();
    }
  };

  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<EventQueue::SubscriberTypeId> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
    auto id = queue.Subscribe<EventType>(std::move(callback));
    eventUnsubscribers_.push_back(id);
  }
};

}  // namespace eillance::view_model
