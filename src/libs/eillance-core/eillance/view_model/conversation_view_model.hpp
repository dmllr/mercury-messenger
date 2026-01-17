#pragma once

#include <atomic>
#include <functional>
#include <memory>
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
  // Internal implementation that can outlive the ViewModel wrapper
  struct Impl;
  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<std::function<void()>> event_unsubscribers_;

  template<typename EventType>
  void Subscribe(EventQueue& q, std::function<void(const EventType&)> callback);
};

}  // namespace eillance::view_model
