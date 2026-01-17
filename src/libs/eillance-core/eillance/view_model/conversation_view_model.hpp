#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ConversationViewModel {
public:
  explicit ConversationViewModel(const ContactID& peer_id, std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~ConversationViewModel();

  struct State {
    ContactID peer_id;
    std::deque<messaging::HistoryEntryRecord> history;
    bool has_older_messages = false;
  };

  struct Observer {
    virtual ~Observer() = default;
    virtual void OnHistoryUpdated(const State& state) = 0;
  };

  void SetObserver(std::shared_ptr<Observer> observer);

  [[nodiscard]] auto GetState() const -> State;

  void SendText(const std::string& content);
  void LoadOlderMessages();
  void LoadFullMessage(uint64_t message_id);
  void MarkMessagesAsRead(const std::vector<std::uint64_t>& message_ids);
  void AddSystemMessage(const std::string& message);

private:
  void LoadInitialHistory();
  void OnMessageReceived(const ContactID& peer_id, const messaging::HistoryEntryRecord& message);
  void OnMessageStatusChanged(const ContactID& peer_id, uint64_t message_id, messaging::MessageStatus status);

  template<typename EventType>
  void Subscribe(EventQueue& q, std::function<void(const EventType&)> callback);

  const ContactID peer_id_;
  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  State state_;

  std::size_t chat_pages_loaded_ = 0;
  std::vector<std::function<void()>> event_unsubscribers_;
  std::thread initial_load_thread_;

  static constexpr size_t kPageSize = 50;
  static inline std::atomic<std::uint64_t> next_system_msg_id_{10000000000000000000ULL};
};

}  // namespace eillance::view_model
