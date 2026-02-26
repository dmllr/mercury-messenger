#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/events/service_connection_state.hpp"
#include "eillance/utils/event_queue.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class SettingsViewModel {
 public:
  explicit SettingsViewModel(std::shared_ptr<Client> client)
      : impl_(std::make_shared<Impl>(std::move(client))) {
    auto& queue = EventQueue::Instance();
    auto impl = impl_;
    Subscribe<EventStatusChanged>(queue, [impl](EventStatusChanged const& event) { impl->OnStatusChanged(event); });
    impl_->LoadSettings();
  }

  ~SettingsViewModel() {
    for (auto const& unsubscribe : eventUnsubscribers_) {
      unsubscribe();
    }
  }

  [[nodiscard]] auto GetState() const -> SettingsState {
    return impl_->GetState();
  }

  void UpdateSignalingUrl(std::string const& url) {
    impl_->UpdateSignalingUrl(url);
  }
  void SetPeerInactivityTimeout(int seconds) {
    impl_->SetPeerInactivityTimeout(seconds);
  }
  void
  UpdateIceConfig(std::string const& stun, std::string const& turn, std::string const& user, std::string const& pass) {
    impl_->UpdateIceConfig(stun, turn, user, pass);
  }
  void SetSoundSettings(bool send, bool receive) {
    impl_->SetSoundSettings(send, receive);
  }

 private:
  struct Impl : std::enable_shared_from_this<Impl> {
    std::shared_ptr<Client> client;

    mutable std::mutex mutex;
    SettingsState state;

    explicit Impl(std::shared_ptr<Client> client)
        : client(std::move(client)) {
    }

    auto GetState() const -> SettingsState {
      std::lock_guard<std::mutex> const lock(mutex);
      return state;
    }

    void UpdateSignalingUrl(std::string const& url) {
      client->UpdateSignalingUrl(url);
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.signaling_server_url = url;
        NotifySettings();
      }
    }

    void SetPeerInactivityTimeout(int seconds) {
      client->GetSettingsManager()->SetPeerInactivityTimeout(std::chrono::seconds(seconds));
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.peer_inactivity_timeout_seconds = seconds;
        NotifySettings();
      }
    }

    void UpdateIceConfig(
        std::string const& stun,
        std::string const& turn,
        std::string const& user,
        std::string const& pass
    ) {
      client->UpdateIceConfig(stun, turn, user, pass);
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.custom_stun = stun;
        state.custom_turn = turn;
        state.custom_turn_user = user;
        state.custom_turn_pass = pass;
        NotifySettings();
      }
    }

    void SetSoundSettings(bool send, bool receive) {
      auto* mgr = client->GetSettingsManager();
      mgr->SetPlaySoundOnSend(send);
      mgr->SetPlaySoundOnReceive(receive);
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.play_sound_on_send = send;
        state.play_sound_on_receive = receive;
        NotifySettings();
      }
    }

    void LoadSettings() {
      std::lock_guard<std::mutex> const lock(mutex);
      LoadSettingsInternal();
    }

    // Helper to load settings without locking, for use when lock is already held
    void LoadSettingsInternal() {
      auto* mgr = client->GetSettingsManager();
      state.signaling_server_url = mgr->GetSignalingServerUrl();
      state.peer_inactivity_timeout_seconds = static_cast<int>(mgr->GetPeerInactivityTimeout().count());
      state.custom_stun = mgr->GetCustomStun();
      state.custom_turn = mgr->GetCustomTurn();
      state.custom_turn_user = mgr->GetCustomTurnUser();
      state.custom_turn_pass = mgr->GetCustomTurnPass();
    }

    void NotifySettings() {
      EventQueue::Instance().Push(EventSettingsStateUpdated{state});
    }

    void OnStatusChanged(EventStatusChanged const& event) {
      if (event.source == EventSource::system) {
        std::lock_guard<std::mutex> const lock(mutex);
        state.connection_state = static_cast<ServiceConnectionState>(event.state);
        if (state.connection_state == ServiceConnectionState::online) {
          LoadSettingsInternal();
        }
        EventQueue::Instance().Push(EventConnectionStateChanged{state.connection_state});
        if (state.connection_state == ServiceConnectionState::online) {
          NotifySettings();
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
