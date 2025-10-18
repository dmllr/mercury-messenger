#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "eillance/events/service_connection_state.h"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/messaging/message_history.hpp"
#include "eillance/rtc/client.hpp"

namespace eillance::view_model {

// UIState holds all the data required for the UI to render its current state.
// It is designed to be immutable once obtained by the UI to ensure thread safety.
struct UIState {
  std::string my_id;
  ServiceConnectionState connection_state = ServiceConnectionState::unknown;
  std::vector<identity::Contact> contacts;
  std::optional<std::string> selected_contact_id;
  std::deque<std::string> chat_history;
  bool has_older_messages = false;
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
        if (arg.empty()) {
            AddSystemMessageToChat("--> Usage: /accept <contact_id>");
        } else {
            std::vector<std::string> matches;
            {
                const std::lock_guard<std::mutex> lock(state_mutex_);
                for (const auto& contact : current_state_.contacts) {
                    if (contact.peer_id.starts_with(arg)) {
                        matches.push_back(contact.peer_id);
                    }
                }
            }

            if (matches.empty()) {
                AddSystemMessageToChat("--> Contact not found for ID starting with: " + arg);
            } else if (matches.size() > 1) {
                AddSystemMessageToChat("--> Ambiguous ID. Multiple contacts start with: " + arg);
            } else {
                AddSystemMessageToChat("--> Accepting contact: " + matches[0]);
                rtc_client_->ConfirmContact(matches[0]);
            }
        }
      } else if (command == "/history") {
          if (arg == "load") {
              LoadOlderMessages();
          } else {
              AddSystemMessageToChat("--> Unknown history command. Usage: /history load");
          }
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
          total_messages_in_history_[*selected_id]++;
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
      if (new_selection) {
          LoadInitialChatHistory(*new_selection);
      }
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
  template<typename K, typename V>
  static V GetValueOrDefault(const std::unordered_map<K, V>& map, const K& key, const V& default_value) {
      auto it = map.find(key);
      if (it != map.end()) {
          return it->second;
      }
      return default_value;
  }

  void LoadInitialChatHistory(const std::string& peer_id) {
      // This must be called from a locked context.
      if (chat_pages_loaded_.contains(peer_id)) {
          return; // Already loaded.
      }

      auto page = rtc_client_->GetMessageHistoryPage(peer_id, 0, kChatPageSize);
      total_messages_in_history_[peer_id] = page.total_messages;
      chat_pages_loaded_[peer_id] = 1;

      std::deque<std::string> formatted_history;
      for (const auto& msg : page.messages) {
          formatted_history.push_back(FormatMessage(peer_id, msg));
      }
      all_chat_histories_[peer_id] = std::move(formatted_history);
  }

  void LoadOlderMessages() {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      if (!current_state_.selected_contact_id) {
          return;
      }
      const auto& peer_id = *current_state_.selected_contact_id;
      const size_t pages_loaded = GetValueOrDefault(chat_pages_loaded_, peer_id, size_t{0});
      const size_t total_messages = GetValueOrDefault(total_messages_in_history_, peer_id, size_t{0});

      if (pages_loaded * kChatPageSize >= total_messages) {
          AddSystemMessageToChat("--> No more messages to load.");
          return;
      }

      auto page = rtc_client_->GetMessageHistoryPage(peer_id, pages_loaded, kChatPageSize);
      for (auto it = page.messages.rbegin(); it != page.messages.rend(); ++it) {
          all_chat_histories_[peer_id].push_front(FormatMessage(peer_id, *it));
      }
      chat_pages_loaded_[peer_id]++;
      UpdateChatHistoryForSelectedContact();
      NotifyStateChanged();
  }

  std::string FormatMessage(const std::string& peer_id, const messaging::HistoryMessage& msg) {
      std::string prefix;
      if (msg.direction == messaging::MessageDirection::Sent) {
          prefix = "[you]: ";
      } else {
          const std::string display_id = peer_id.length() > 8 ? peer_id.substr(0, 8) + "..." : peer_id;
          prefix = "[" + display_id + "]: ";
      }
      return prefix + msg.content;
  }

  void SetupRtcClientCallbacks() {
    rtc_client_->SetChatMessageReceivedCallback([this](const std::string& peer_id, const messaging::HistoryMessage& message) {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      all_chat_histories_[peer_id].push_back(FormatMessage(peer_id, message));
      total_messages_in_history_[peer_id]++;

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
      history.push_back("--> Use '/accept " + peer_id.substr(0, 12) + "...' to confirm.");
      total_messages_in_history_[peer_id] += 2;
      if (current_state_.selected_contact_id && *current_state_.selected_contact_id == peer_id) {
        UpdateChatHistoryForSelectedContact();
      }
      NotifyStateChanged();
    });

    rtc_client_->SetContactStatusChangedCallback([this](const std::string& peer_id, identity::ContactStatus /*status*/) {
      const std::lock_guard<std::mutex> lock(state_mutex_);
      current_state_.contacts = rtc_client_->GetContacts();
      const std::string display_id = peer_id.length() > 12 ? peer_id.substr(0, 12) + "..." : peer_id;
      all_chat_histories_[peer_id].push_back("--> Contact status for " + display_id + " updated.");
      total_messages_in_history_[peer_id]++;
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
        const auto& peer_id = *current_state_.selected_contact_id;
        current_state_.chat_history = all_chat_histories_[peer_id];
        const size_t pages_loaded = GetValueOrDefault(chat_pages_loaded_, peer_id, size_t{0});
        const size_t total_messages = GetValueOrDefault(total_messages_in_history_, peer_id, size_t{0});
        current_state_.has_older_messages = (pages_loaded * kChatPageSize) < total_messages;
    } else {
      current_state_.chat_history.clear();
      current_state_.has_older_messages = false;
    }
  }
  void AddSystemMessageToChat(const std::string& message) {
    const std::lock_guard<std::mutex> lock(state_mutex_);
    if (current_state_.selected_contact_id) {
      auto& history = all_chat_histories_[*current_state_.selected_contact_id];
      history.push_back(message);
      total_messages_in_history_[*current_state_.selected_contact_id]++;
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

  // Per-peer-ID state
  std::unordered_map<std::string, std::deque<std::string>> all_chat_histories_;
  std::unordered_map<std::string, size_t> chat_pages_loaded_;
  std::unordered_map<std::string, size_t> total_messages_in_history_;

  static constexpr size_t kChatPageSize = 50;

  std::function<void()> on_state_changed_callback_;
};

}  // namespace eillance::view_model
