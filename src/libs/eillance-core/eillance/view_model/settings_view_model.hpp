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
    Subscribe<EventServiceConnectionStateChanged>(queue, [impl](auto const& event) { impl->OnStatusChanged(event); });
    impl_->LoadSettings();
  }

  ~SettingsViewModel() {
    for (auto const id : eventUnsubscribers_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

  [[nodiscard]] auto GetState() const -> SettingsState {
    return impl_->GetState();
  }

  void UpdateBootstrapNodeUrl(std::string const& url) {
    impl_->UpdateBootstrapNodeUrl(url);
  }
  void SetPeerInactivityTimeout(int seconds) {
    impl_->SetPeerInactivityTimeout(seconds);
  }
  void UpdateIceConfig(IceConfigParams const& params) {
    impl_->UpdateIceConfig(params);
  }
  void SetSoundSettings(bool send, bool receive) {
    impl_->SetSoundSettings(send, receive);
  }
  void SetShowNotificationContent(bool show) {
    impl_->SetShowNotificationContent(show);
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

    void UpdateBootstrapNodeUrl(std::string const& url) {
      client->UpdateBootstrapNodeUrl(url);
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.bootstrap_node_url = url;
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

    void UpdateIceConfig(IceConfigParams const& params) {
      client->UpdateIceConfig(params);
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.custom_stun = params.stun;
        state.custom_turn = params.turn;
        state.custom_turn_user = params.turn_user;
        state.custom_turn_pass = params.turn_pass;
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

    void SetShowNotificationContent(bool show) {
      client->GetSettingsManager()->SetShowNotificationContent(show);
      {
        std::lock_guard<std::mutex> const lock(mutex);
        state.show_notification_content = show;
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
      state.bootstrap_node_url = mgr->GetBootstrapNodeUrl();
      state.peer_inactivity_timeout_seconds = static_cast<int>(mgr->GetPeerInactivityTimeout().count());
      state.custom_stun = mgr->GetCustomStun();
      state.custom_turn = mgr->GetCustomTurn();
      state.custom_turn_user = mgr->GetCustomTurnUser();
      state.custom_turn_pass = mgr->GetCustomTurnPass();
      state.show_notification_content = mgr->GetShowNotificationContent();
    }

    void NotifySettings() {
      EventQueue::Instance().Push(EventSettingsStateUpdated{state});
    }

    void OnStatusChanged(EventServiceConnectionStateChanged const& event) {
      std::lock_guard<std::mutex> const lock(mutex);
      state.connection_state = event.state;
      if (state.connection_state == ServiceConnectionState::online) {
        LoadSettingsInternal();
      }
      EventQueue::Instance().Push(EventConnectionStateChanged{state.connection_state});
      if (state.connection_state == ServiceConnectionState::online) {
        NotifySettings();
      }
    }
  };

  std::shared_ptr<Impl> impl_;

  std::vector<EventQueue::SubscriberTypeId> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback) {
    auto event_id = queue.Subscribe<EventType>(std::move(callback));
    eventUnsubscribers_.push_back(event_id);
  }
};

}  // namespace eillance::view_model
