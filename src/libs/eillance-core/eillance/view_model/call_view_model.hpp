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

  void SetObserver(std::shared_ptr<Observer> observer) const;

  [[nodiscard]] auto GetState() const -> CallState;

  void StartVoiceCall(ContactID const& peer_id);
  void StartVideoCall(ContactID const& peer_id);
  void Hangup();
  void AcceptCall();
  void RejectCall();

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;

  std::vector<std::function<void()>> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback);
};

}  // namespace eillance::view_model
