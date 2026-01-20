#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class CallViewModel {
 public:
  struct Observer {
    Observer() = default;
    Observer(Observer const&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(Observer const&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnCallStateChanged(CallState const& state) = 0;
  };

  explicit CallViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~CallViewModel();

  void SetObserver(std::shared_ptr<Observer> observer);

  [[nodiscard]] auto GetState() const -> CallState;

  // Actions
  void StartVoiceCall(ContactID const& peerId);
  void StartVideoCall(ContactID const& peerId);
  void Hangup();
  void AcceptCall();
  void RejectCall();

 private:
  void OnIncomingCall(ContactID const& peerId, messaging::HistoryEntryKind callType);
  void OnCallRinging(ContactID const& peerId);
  void OnCallAccepted(ContactID const& peerId);
  void OnCallTerminated(ContactID const& peerId);
  void Notify() const;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback);

  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  CallState state_;
  std::vector<std::function<void()>> eventUnsubscribers_;
};

}  // namespace eillance::view_model
