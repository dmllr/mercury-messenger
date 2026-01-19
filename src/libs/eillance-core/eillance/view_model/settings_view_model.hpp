#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class SettingsViewModel {
public:
  struct Observer {
    Observer() = default;
    Observer(const Observer&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(const Observer&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnSettingsUpdated(const std::optional<std::string>& signalingUrl, int timeout) = 0;
    virtual void OnConnectionStateChanged(ServiceConnectionState state) = 0;
  };

  explicit SettingsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~SettingsViewModel();

  void SetObserver(std::shared_ptr<Observer> observer);

  struct State {
    std::optional<std::string> signaling_server_url;
    int peer_inactivity_timeout_seconds = 120;
    ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  };

  [[nodiscard]] auto GetState() const -> State;

  // Actions
  void UpdateSignalingUrl(const std::string& url);
  void SetPeerInactivityTimeout(int seconds);

private:
  void LoadSettings();

  template<typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(const EventType&)> callback);

  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  State state_;
  std::vector<std::function<void()>> eventUnsubscribers_;
};

}  // namespace eillance::view_model
