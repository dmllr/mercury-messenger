#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class SettingsViewModel {
 public:
  struct Observer {
    Observer() = default;
    Observer(Observer const&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(Observer const&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnSettingsUpdated(std::optional<std::string> const& signaling_url, int timeout) = 0;
    virtual void OnConnectionStateChanged(ServiceConnectionState state) = 0;
  };

  explicit SettingsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~SettingsViewModel();

  void SetObserver(std::shared_ptr<Observer> observer) const;

  struct State {
    std::optional<std::string> signaling_server_url;
    int peer_inactivity_timeout_seconds = 120;
    ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  };

  [[nodiscard]] auto GetState() const -> State;

  void UpdateSignalingUrl(std::string const& url);
  void SetPeerInactivityTimeout(int seconds);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;

  std::vector<std::function<void()>> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback);
};

}  // namespace eillance::view_model
