#include "settings_view_model.hpp"

namespace eillance::view_model {

SettingsViewModel::SettingsViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier)
    : client_(std::move(client)), notifier_(std::move(notifier)) {
  LoadSettings();
}

auto SettingsViewModel::GetState() const -> State {
  const std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

void SettingsViewModel::UpdateSignalingUrl(const std::string& url) {
  client_->UpdateSignalingUrl(url);
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.signaling_server_url = url;
  }
  if (notifier_) notifier_(StateChange::SignalingUrl);
}

void SettingsViewModel::SetPeerInactivityTimeout(int seconds) {
  client_->GetSettingsManager()->SetPeerInactivityTimeout(std::chrono::seconds(seconds));
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.peer_inactivity_timeout_seconds = seconds;
  }
  if (notifier_) notifier_(StateChange::PeerInactivityTimeout);
}

void SettingsViewModel::OnConnectionStateChanged(ServiceConnectionState state) {
  bool changed = false;
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (state_.connection_state != state) {
      state_.connection_state = state;
      changed = true;

      if (state == ServiceConnectionState::online) {
        state_.signaling_server_url = client_->GetSettingsManager()->GetSignalingServerUrl();
      }
    }
  }

  if (changed && notifier_) {
    notifier_(StateChange::Connection);
    if (state == ServiceConnectionState::online) {
      notifier_(StateChange::SignalingUrl);
    }
  }
}

void SettingsViewModel::LoadSettings() {
  const std::lock_guard<std::mutex> lock(mutex_);
  state_.signaling_server_url = client_->GetSettingsManager()->GetSignalingServerUrl();
  state_.peer_inactivity_timeout_seconds = static_cast<int>(client_->GetSettingsManager()->GetPeerInactivityTimeout().count());
}

}  // namespace eillance::view_model
