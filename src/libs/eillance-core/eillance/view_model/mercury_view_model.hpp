#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/utils/dispatch_queue.hpp"

#include "call_view_model.hpp"
#include "contacts_view_model.hpp"
#include "conversation_view_model.hpp"
#include "profile_view_model.hpp"
#include "settings_view_model.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

// Aggregated state struct for backward compatibility / easy snapshotting
struct UIState {
  MyProfile my_profile;
  ServiceConnectionState connection_state;
  std::vector<ViewContact> contacts;
  std::optional<ContactID> selected_contact_id;
  std::deque<messaging::HistoryEntryRecord> interaction_history;
  bool has_older_messages;
  CallState call_state;
  std::optional<std::string> signaling_server_url;
  int peer_inactivity_timeout_seconds;
};

class MercuryViewModel {
public:
  explicit MercuryViewModel(std::shared_ptr<Client> client);
  ~MercuryViewModel();

  MercuryViewModel(const MercuryViewModel&) = delete;
  auto operator=(const MercuryViewModel&) -> MercuryViewModel& = delete;

  // Accessors for Sub-ViewModels
  [[nodiscard]] auto Contacts() const -> ContactsViewModel* { return contacts_vm_.get(); }
  [[nodiscard]] auto Conversation() const -> ConversationViewModel* { return conversation_vm_.get(); }
  [[nodiscard]] auto Call() const -> CallViewModel* { return call_vm_.get(); }
  [[nodiscard]] auto Profile() const -> ProfileViewModel* { return profile_vm_.get(); }
  [[nodiscard]] auto Settings() const -> SettingsViewModel* { return settings_vm_.get(); }

  // Global actions
  void RefreshState();

  // Legacy / Aggregate State Accessor (constructs UIState from sub-models)
  [[nodiscard]] auto GetState() const -> UIState;

  // State Change Notification
  void SetOnStateChangedCallback(std::function<void(const std::vector<StateChange>&)> callback);

private:
  void SetupClientCallbacks();
  void Notify(StateChange change);
  void NotifyBatch(const std::vector<StateChange>& changes);

  std::shared_ptr<Client> client_;
  DispatchQueue dispatch_queue_{"ViewModel", 1};
  std::function<void(const std::vector<StateChange>&)> on_state_changed_callback_;

  // Sub-ViewModels
  std::unique_ptr<ContactsViewModel> contacts_vm_;
  std::unique_ptr<ConversationViewModel> conversation_vm_;
  std::unique_ptr<CallViewModel> call_vm_;
  std::unique_ptr<ProfileViewModel> profile_vm_;
  std::unique_ptr<SettingsViewModel> settings_vm_;
};

}  // namespace eillance::view_model
