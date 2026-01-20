#include "settings_view_model.hpp"

namespace eillance::view_model {

SettingsViewModel::SettingsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : client_(std::move(client)),
      router_(std::move(router)) {
  auto& queue = router_->Queue();
  Subscribe<EventStatusChanged>(queue, [this](EventStatusChanged const& event) {
    if (event.source == EventSource::system) {
      std::lock_guard<std::mutex> const lock(mutex_);
      state_.connection_state = static_cast<ServiceConnectionState>(event.state);
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
  for (auto const& unsubscribe : eventUnsubscribers_) {
    unsubscribe();
  }
}

template <typename EventType>
void SettingsViewModel::Subscribe(EventQueue& queue, std::function<void(EventType const&)> callback) {
  auto id = queue.Subscribe<EventType>(std::move(callback));
  eventUnsubscribers_.emplace_back([&queue, id] { queue.Unsubscribe<EventType>(id); });
}

void SettingsViewModel::SetObserver(std::shared_ptr<Observer> observer) {
  std::lock_guard<std::mutex> const lock(mutex_);
  observer_ = std::move(observer);
  if (observer_) {
    observer_->OnConnectionStateChanged(state_.connection_state);
    observer_->OnSettingsUpdated(state_.signaling_server_url, state_.peer_inactivity_timeout_seconds);
  }
}

auto SettingsViewModel::GetState() const -> State {
  std::lock_guard<std::mutex> const lock(mutex_);
  return state_;
}

void SettingsViewModel::UpdateSignalingUrl(std::string const& url) {
  client_->UpdateSignalingUrl(url);
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    state_.signaling_server_url = url;
    if (observer_) {
      observer_->OnSettingsUpdated(state_.signaling_server_url, state_.peer_inactivity_timeout_seconds);
    }
  }
}

void SettingsViewModel::SetPeerInactivityTimeout(int seconds) {
  client_->GetSettingsManager()->SetPeerInactivityTimeout(std::chrono::seconds(seconds));
  {
    std::lock_guard<std::mutex> const lock(mutex_);
    state_.peer_inactivity_timeout_seconds = seconds;
    if (observer_) {
      observer_->OnSettingsUpdated(state_.signaling_server_url, state_.peer_inactivity_timeout_seconds);
    }
  }
}

void SettingsViewModel::LoadSettings() {
  std::lock_guard<std::mutex> const lock(mutex_);
  state_.signaling_server_url = client_->GetSettingsManager()->GetSignalingServerUrl();
  state_.peer_inactivity_timeout_seconds =
      static_cast<int>(client_->GetSettingsManager()->GetPeerInactivityTimeout().count());
}

}  // namespace eillance::view_model
