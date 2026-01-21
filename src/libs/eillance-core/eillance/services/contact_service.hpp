#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "eillance/client/device.hpp"
#include "eillance/identity/contact_manager.hpp"
#include "eillance/services/account_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"

namespace eillance::services {

class ContactService {
 public:
  using ConnectionRequestHandler = std::function<void(ContactID const&)>;

  explicit ContactService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport,
      AccountService* account_service,
      ConnectionRequestHandler connection_handler
  );

  void SetMessagingService(MessagingService* messaging_service);

  // Actions
  void AddContact(ContactID const& peer_id, std::optional<std::vector<std::byte>> const& token);
  void ConfirmContact(ContactID const& peer_id);
  void BlockContact(ContactID const& peer_id, bool notify_peer);
  void DeleteContact(ContactID const& peer_id);
  void UnblockContact(ContactID const& peer_id);

  void SetContactDisplayNameOverride(ContactID const& peer_id, std::string const& name);
  void ResetContactDisplayNameOverride(ContactID const& peer_id);

  auto GenerateContactToken() -> std::vector<std::byte>;

  // Queries
  [[nodiscard]] auto GetContacts() const -> std::vector<identity::Contact>;
  [[nodiscard]] auto GetContact(ContactID const& peer_id) -> std::optional<identity::Contact>;
  [[nodiscard]] auto IsConfirmed(ContactID const& peer_id) const -> bool;

  // Event Handlers
  void OnContactAddRequest(
      ContactID const& peer_id,
      std::optional<std::string> const& token,
      nlohmann::json const& profile_json
  );
  void OnContactConfirm(ContactID const& peer_id);
  void OnContactRemoved(ContactID const& peer_id);
  void OnProfileUpdate(ContactID const& peer_id, nlohmann::json const& profile_json);
  void OnReconfirmRequest(ContactID const& peer_id);
  void OnReconfirmResponse(ContactID const& peer_id, std::string const& signature_hex);
  void OnContactSignature(ContactID const& peer_id, std::string const& signature_hex);
  void OnDeviceProfileReceived(ContactID const& peer_id, nlohmann::json const& profile_json);

  // Callbacks
  using OnContactRequestCallback = std::function<void(ContactID const& peer_id)>;
  using OnContactStatusChangedCallback = std::function<void(ContactID const& peer_id, identity::ContactStatus status)>;
  using OnContactProfileUpdatedCallback = std::function<void(ContactID const& peer_id)>;
  using OnContactRemovedCallback = std::function<void(ContactID const& peer_id)>;
  using OnPeerListUpdateCallback = std::function<void(std::vector<Device> const&)>;
  using OnProfileReceivedCallback = std::function<void(ContactID const&, Device::Profile const&)>;

  void SetContactRequestCallback(OnContactRequestCallback cb);
  void SetContactStatusChangedCallback(OnContactStatusChangedCallback cb);
  void SetContactProfileUpdatedCallback(OnContactProfileUpdatedCallback cb);
  void SetContactRemovedCallback(OnContactRemovedCallback cb);
  void SetPeerListUpdateCallback(OnPeerListUpdateCallback cb);
  void SetProfileReceivedCallback(OnProfileReceivedCallback cb);

 private:
  void NotifyPeerListUpdate();
  static auto ParseDeviceProfile(nlohmann::json const& jprofile) -> Device::Profile;

  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;
  std::shared_ptr<identity::ContactManager> manager_;

  AccountService* account_service_;
  MessagingService* messaging_service_{nullptr};
  ConnectionRequestHandler connection_handler_;

  std::unordered_map<ContactID, Device> online_peers_;
  mutable std::mutex online_peers_mutex_;

  OnContactRequestCallback on_contact_request_;
  OnContactStatusChangedCallback on_contact_status_changed_;
  OnContactProfileUpdatedCallback on_contact_profile_updated_;
  OnContactRemovedCallback on_contact_removed_;
  OnPeerListUpdateCallback on_peer_list_update_;
  OnProfileReceivedCallback on_profile_received_;
};

}  // namespace eillance::services
