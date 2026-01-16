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
    virtual ~Observer() = default;
    virtual void OnCallStateChanged(const CallState& state) = 0;
  };

  explicit CallViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~CallViewModel();

  void SetObserver(std::shared_ptr<Observer> observer);

  [[nodiscard]] auto GetState() const -> CallState;

  // Actions
  void StartVoiceCall(const ContactID& peer_id);
  void StartVideoCall(const ContactID& peer_id);
  void Hangup();
  void AcceptCall();
  void RejectCall();

private:
  void OnIncomingCall(const ContactID& peer_id, messaging::HistoryEntryKind call_type);
  void OnCallRinging(const ContactID& peer_id);
  void OnCallAccepted(const ContactID& peer_id);
  void OnCallTerminated(const ContactID& peer_id);
  void Notify();

  template<typename EventType>
  void Subscribe(EventQueue& q, std::function<void(const EventType&)> callback);

  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  CallState state_;
  std::vector<std::function<void()>> event_unsubscribers_;
};

}  // namespace eillance::view_model
