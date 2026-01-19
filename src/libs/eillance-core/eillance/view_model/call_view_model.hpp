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
    Observer(const Observer&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(const Observer&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnCallStateChanged(const CallState& state) = 0;
  };

  explicit CallViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~CallViewModel();

  void SetObserver(std::shared_ptr<Observer> observer);

  [[nodiscard]] auto GetState() const -> CallState;

  // Actions
  void StartVoiceCall(const ContactID& peerId);
  void StartVideoCall(const ContactID& peerId);
  void Hangup();
  void AcceptCall();
  void RejectCall();

private:
  void OnIncomingCall(const ContactID& peerId, messaging::HistoryEntryKind callType);
  void OnCallRinging(const ContactID& peerId);
  void OnCallAccepted(const ContactID& peerId);
  void OnCallTerminated(const ContactID& peerId);
  void Notify();

  template<typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(const EventType&)> callback);

  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  CallState state_;
  std::vector<std::function<void()>> eventUnsubscribers_;
};

}  // namespace eillance::view_model
