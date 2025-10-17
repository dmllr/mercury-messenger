#pragma once

#include <algorithm>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/rtc/client.hpp"
#include "eillance/storage/persistent_storage.hpp"

namespace eillance::view_model {

// UIState holds all the data required for the UI to render its current state.
// It is designed to be immutable once obtained by the UI to ensure thread safety.
struct UIState {
  std::string my_id;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::vector<identity::Contact> contacts;
  std::optional<std::string> selected_contact_id;
  std::deque<std::string> chat_history;
};

// MercuryViewModel encapsulates the application logic and state management for Mercury clients.
// It interacts with the RtcClient and provides a clean interface for the UI.
class MercuryViewModel {
public:
  explicit MercuryViewModel(std::shared_ptr<RtcClient> rtc_client)
      : rtc_client_(std::move(rtc_client)) {
    {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.my_id = rtc_client_->GetId();
      current_state_.contacts = rtc_client_->GetContacts();
    }
    SetupRtcClientCallbacks();
  }
  ~MercuryViewModel() = default;

  MercuryViewModel(const MercuryViewModel&) = delete;
  MercuryViewModel& operator=(const MercuryViewModel&) = delete;
  MercuryViewModel(MercuryViewModel&&) = delete;
  MercuryViewModel& operator=(MercuryViewModel&&) = delete;


  // Processes user input from the UI.
  // Returns true if the application should exit.
  auto ProcessUserInput(const std::string& input) -> bool {
    if (input.empty()) {
      return false;
    }

    if (input.starts_with("/")) {  // Command processing.
      std::stringstream ss(input);
      std::string command;
      std::string arg;
      ss >> command;
      std::getline(ss >> std::ws, arg);

      if (command == "/add") {
        AddSystemMessageToChat("--> Adding contact: " + arg);
        rtc_client_->AddContact(arg);
      } else if (command == "/accept") {
        AddSystemMessageToChat("--> Accepting contact: " + arg);
        rtc_client_->ConfirmContact(arg);
      } else if (command == "/exit") {
        return true;  // Signal exit to the UI.
      } else {
        AddSystemMessageToChat("--> Unknown command: " + command);
      }
    } else {  // Message processing.
      std::optional<std::string> selected_id;
      {
        const std::lock_guard<std::mutex> lock(state_mutex_);
        selected_id = current_state_.selected_contact_id;
      }

      if (!selected_id) {
        AddSystemMessageToChat("--> Select a contact before sending a message.");
      } else {
        {
          const std::lock_guard<std::mutex> lock(state_mutex_);
          auto& history = all_chat_histories_[*selected_id];
          history.push_back("[you]: " + input);
          if (history.size() > kMaxChatHistory) {
            history.pop_front();
          }
          UpdateChatHistoryForSelectedContact();
        }
        rtc_client_->SendChatMessage(*selected_id, input);
      }
    }
    NotifyStateChanged();
    return false;
  }

  // Informs the ViewModel about the currently selected contact in the UI.
  void SelectContactByIndex(size_t index) {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    std::optional<std::string> new_selection;
    if (index < current_state_.contacts.size()) {
      new_selection = current_state_.contacts[index].peer_id;
    }

    if (current_state_.selected_contact_id != new_selection) {
      current_state_.selected_contact_id = new_selection;
      UpdateChatHistoryForSelectedContact();
    }
  }

  // Retrieves the current state for UI rendering.
  [[nodiscard]] auto GetState() const -> UIState {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    return current_state_;
  }

  // Sets a callback to be invoked when the UI state changes and a redraw is needed.
  void SetOnStateChangedCallback(std::function<void()> callback) {
    on_state_changed_callback_ = std::move(callback);
  }

private:
  void SetupRtcClientCallbacks() {
    rtc_client_->SetChatMessageReceivedCallback([this](const std::string& peer_id, const std::string& message) {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      const std::string display_id = peer_id.length() > 8 ? peer_id.substr(0, 8) + "..." : peer_id;
      auto& history = all_chat_histories_[peer_id];
      history.push_back("[" + display_id + "]: " + message);
      if (history.size() > kMaxChatHistory) {
        history.pop_front();
      }
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        UpdateChatHistoryForSelectedContact();
      }
      NotifyStateChanged();
    });

    rtc_client_->SetContactRequestCallback([this](const std::string& peer_id) {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.contacts = rtc_client_->GetContacts();
      auto& history = all_chat_histories_[peer_id];
      history.push_back("--> Contact request from " + peer_id);
      history.push_back("--> Use '/accept " + peer_id + "' to confirm.");
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        UpdateChatHistoryForSelectedContact();
      }
      NotifyStateChanged();
    });

    rtc_client_->SetContactStatusChangedCallback([this](const std::string& peer_id, identity::ContactStatus /*status*/) {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.contacts = rtc_client_->GetContacts();
      const std::string display_id = peer_id.length() > 12 ? peer_id.substr(0, 12) + "..." : peer_id;
      auto& history = all_chat_histories_[peer_id];
      history.push_back("--> Contact status for " + display_id + " updated.");
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        UpdateChatHistoryForSelectedContact();
      }
      NotifyStateChanged();
    });

    rtc_client_->SetStatusCallback([this](const std::string& /*peer_id*/, EventSource source, int state_val) {
      if (source == EventSource::system) {
        auto state = static_cast<ServiceConnectionState>(state_val);
        {
          const std::lock_guard<std::mutex> lock(state_mutex_);
          current_state_.connection_state = state;
        }
        NotifyStateChanged();
      }
    });
  }
  void UpdateChatHistoryForSelectedContact() {
    // This function must be called from within a locked context.
    if (current_state_.selected_contact_id) {
      current_state_.chat_history = all_chat_histories_[*current_state_.selected_contact_id];
    } else {
      current_state_.chat_history.clear();
    }
  }
  void AddSystemMessageToChat(const std::string& message) {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    // Add system message only to the currently selected contact's chat history.
    if (current_state_.selected_contact_id) {
      auto& history = all_chat_histories_[*current_state_.selected_contact_id];
      history.push_back(message);
      if (history.size() > kMaxChatHistory) {
        history.pop_front();
      }
      UpdateChatHistoryForSelectedContact();
    }
  }
  void NotifyStateChanged() const {
    if (on_state_changed_callback_) {
      on_state_changed_callback_();
    }
  }

  std::shared_ptr<RtcClient> rtc_client_;

  mutable std::mutex state_mutex_;
  UIState current_state_;
  std::unordered_map<std::string, std::deque<std::string>> all_chat_histories_;
  static constexpr size_t kMaxChatHistory = 50;

  std::function<void()> on_state_changed_callback_;
};

}  // namespace eillance::view_model
