#pragma once

#include <chrono>
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

  explicit SettingsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : impl_(std::make_shared<Impl>(std::move(client), std::move(router))) {
    auto& queue = impl_->router->Queue();
    auto impl = impl_;
    Subscribe<EventStatusChanged>(queue, [impl](EventStatusChanged const& event) { impl->OnStatusChanged(event); });
    impl_->LoadSettings();
  }

  ~SettingsViewModel() {
    for (auto const& unsubscribe : eventUnsubscribers_) {
      unsubscribe();
    }
  }

  void SetObserver(std::shared_ptr<Observer> observer) const { impl_->SetObserver(std::move(observer)); }

  struct State {
    std::optional<std::string> signaling_server_url;
    int peer_inactivity_timeout_seconds = 120;
    ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  };

  [[nodiscard]] auto GetState() const -> State { return impl_->GetState(); }

  void UpdateSignalingUrl(std::string const& url) { impl_->UpdateSignalingUrl(url); }
  void SetPeerInactivityTimeout(int seconds) { impl_->SetPeerInactivityTimeout(seconds); }

 private:
  struct Impl : std::enable_shared_from_this<Impl> {
    std::shared_ptr<Client> client;
    std::shared_ptr<ClientEventRouter> router;
    std::shared_ptr<Observer> observer;

    mutable std::mutex mutex;
    State state;

    Impl(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
        : client(std::move(client)),
          router(std::move(router)) {}

    void SetObserver(std::shared_ptr<Observer> obs) {
      std::lock_guard<std::mutex> const lock(mutex);
      observer = std::move(obs);
      if (observer) {
        observer->OnConnectionStateChanged(state.connection_state);
        observer->OnSettingsUpdated(state.signaling_server_url, state.peer_inactivity_timeout_seconds);
      }
    }

    auto GetState() const -> State {
      std::lock_guard<std::mutex> const lock(mutex);
      return state;
    }

    void UpdateSignalingUrl(std::string const& url) {
      client->UpdateSignalingUrl(url);
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.signaling_server_url = url;
        if (observer) {
          observer->OnSettingsUpdated(state.signaling_server_url, state.peer_inactivity_timeout_seconds);
        }
      }
    }

    void SetPeerInactivityTimeout(int seconds) {
      client->GetSettingsManager()->SetPeerInactivityTimeout(std::chrono::seconds(seconds));
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.peer_inactivity_timeout_seconds = seconds;
        if (observer) {
          observer->OnSettingsUpdated(state.signaling_server_url, state.peer_inactivity_timeout_seconds);
        }
      }
    }

    void LoadSettings() {
      std::lock_guard<std::mutex> const lock(mutex);
      state.signaling_server_url = client->GetSettingsManager()->GetSignalingServerUrl();
      state.peer_inactivity_timeout_seconds =
          static_cast<int>(client->GetSettingsManager()->GetPeerInactivityTimeout().count());
    }

    void OnStatusChanged(EventStatusChanged const& event) {
      if (event.source == EventSource::system) {
        std::lock_guard<std::mutex> const lock(mutex);
        state.connection_state = static_cast<ServiceConnectionState>(event.state);
        if (state.connection_state == ServiceConnectionState::online) {
          state.signaling_server_url = client->GetSettingsManager()->GetSignalingServerUrl();
        }
        if (observer) {
          observer->OnConnectionStateChanged(state.connection_state);
          if (state.connection_state == ServiceConnectionState::online) {
            observer->OnSettingsUpdated(state.signaling_server_url, state.peer_inactivity_timeout_seconds);
          }
        }
      }
    }
  };

  std::shared_ptr<Impl> impl_;

  std::vector<std::function<void()>> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
    auto event_id = queue.Subscribe<EventType>(std::move(callback));
    eventUnsubscribers_.emplace_back([&queue, event_id] { queue.Unsubscribe<EventType>(event_id); });
  }
};

}  // namespace eillance::view_model
