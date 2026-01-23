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
  explicit ConversationViewModel(
      ContactID const& peer_id,
      std::shared_ptr<Client> client,
      std::shared_ptr<ClientEventRouter> router
  );
  ~ConversationViewModel();

  struct State {
    ContactID peer_id;
    std::deque<messaging::HistoryEntryRecord> history{};
    bool has_older_messages = false;
  };

  struct Observer {
    Observer() = default;
    Observer(Observer const&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(Observer const&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnHistoryUpdated(State const& state) = 0;
  };

  void SetObserver(std::shared_ptr<Observer> observer) const;

  [[nodiscard]] auto GetState() const -> State;

  void SendText(std::string const& content) const;
  void SendMessage(std::string const& content, std::vector<services::AttachmentRequest> const& attachments) const;
  void LoadOlderMessages() const;
  void LoadFullMessage(uint64_t message_id) const;
  void MarkMessagesAsRead(std::vector<std::uint64_t> const& message_ids) const;
  void AddSystemMessage(std::string const& message) const;

 private:
  // Internal implementation that can outlive the ViewModel wrapper
  struct Impl;
  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<std::function<void()>> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback);
};

}  // namespace eillance::view_model
