#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ConversationViewModel {
public:
  explicit ConversationViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier);

  struct State {
    std::optional<ContactID> selected_contact_id;
    std::deque<messaging::HistoryEntryRecord> history;
    bool has_older_messages = false;
  };

  [[nodiscard]] auto GetState() const -> State;
  [[nodiscard]] auto GetSelectedContactId() const -> std::optional<ContactID>;

  // Actions
  void SelectContact(const ContactID& peer_id);
  void DeselectContact();
  void SendMessage(const std::string& content);
  void LoadOlderMessages();
  void LoadFullMessage(uint64_t message_id);
  void MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids);

  // Internal helper for system messages (e.g. from CLI)
  void AddSystemMessage(const std::string& message);

  // Updates from Client
  void OnMessageReceived(const ContactID& peer_id, const messaging::HistoryEntryRecord& message);
  void OnMessageStatusChanged(const ContactID& peer_id, uint64_t message_id, messaging::MessageStatus status);

  // Debug
  void DebugEraseAllHistory();

private:
  void LoadInitialHistory(const ContactID& peer_id);

  std::shared_ptr<Client> client_;
  std::function<void(StateChange)> notifier_;

  mutable std::mutex mutex_;
  State state_;

  std::unordered_map<ContactID, size_t> chat_pages_loaded_;
  static constexpr size_t kPageSize = 50;
  static inline std::atomic<std::uint64_t> next_system_msg_id_{10000000000000000000ULL};
};

}  // namespace eillance::view_model
