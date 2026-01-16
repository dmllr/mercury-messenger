#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "eillance/client/client.hpp"
#include "eillance/events/service_connection_state.h"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class SettingsViewModel {
public:
  explicit SettingsViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier);

  struct State {
    std::optional<std::string> signaling_server_url;
    int peer_inactivity_timeout_seconds = 120;
    ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  };

  [[nodiscard]] auto GetState() const -> State;

  // Actions
  void UpdateSignalingUrl(const std::string& url);
  void SetPeerInactivityTimeout(int seconds);

  // Updates from Client
  void OnConnectionStateChanged(ServiceConnectionState state);

  // Internal initialization
  void LoadSettings();

private:
  std::shared_ptr<Client> client_;
  std::function<void(StateChange)> notifier_;

  mutable std::mutex mutex_;
  State state_;
};

}  // namespace eillance::view_model
