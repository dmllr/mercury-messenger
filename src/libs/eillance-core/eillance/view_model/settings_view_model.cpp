#include "settings_view_model.hpp"

namespace eillance::view_model {

SettingsViewModel::SettingsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : client_(std::move(client)), router_(std::move(router)) {
  auto& q = router_->Queue();
  Subscribe<EventStatusChanged>(q, [this](const EventStatusChanged& e) {
    if (e.source == EventSource::system) {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.connection_state = static_cast<ServiceConnectionState>(e.state);
      if (state_.connection_state == ServiceConnectionState::online) {
        state_.signaling_server_url = client_->GetSettingsManager()->GetSignalingServerUrl();
      }
      if (observer_) {
        observer_->OnConnectionStateChanged(state_.connection_state);
        if (state_.connection_state == ServiceConnectionState::online) {
          observer_->OnSettingsUpdated(state_.signaling_server_url, state_.peer_inactivity_timeout_seconds);
        }
      }
    }
  });
  LoadSettings();
}

SettingsViewModel::~SettingsViewModel() {
  for (const auto& unsubscribe: event_unsubscribers_) {
    unsubscribe();
  }
}

template<typename EventType>
void SettingsViewModel::Subscribe(EventQueue& q, std::function<void(const EventType&)> callback) {
  auto id = q.Subscribe<EventType>(std::move(callback));
  event_unsubscribers_.push_back([&q, id] { q.Unsubscribe<EventType>(id); });
}

void SettingsViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  std::lock_guard<std::mutex> lock(mutex_);
  observer_ = std::move(observer);
  if (observer_) {
    observer_->OnConnectionStateChanged(state_.connection_state);
    observer_->OnSettingsUpdated(state_.signaling_server_url, state_.peer_inactivity_timeout_seconds);
  }
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
    if (observer_) {
      observer_->OnSettingsUpdated(state_.signaling_server_url, state_.peer_inactivity_timeout_seconds);
    }
  }
}

void SettingsViewModel::SetPeerInactivityTimeout(int seconds) {
  client_->GetSettingsManager()->SetPeerInactivityTimeout(std::chrono::seconds(seconds));
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    state_.peer_inactivity_timeout_seconds = seconds;
    if (observer_) {
      observer_->OnSettingsUpdated(state_.signaling_server_url, state_.peer_inactivity_timeout_seconds);
    }
  }
}

void SettingsViewModel::LoadSettings() {
  const std::lock_guard<std::mutex> lock(mutex_);
  state_.signaling_server_url = client_->GetSettingsManager()->GetSignalingServerUrl();
  state_.peer_inactivity_timeout_seconds = static_cast<int>(client_->GetSettingsManager()->GetPeerInactivityTimeout().count());
}

}  // namespace eillance::view_model
