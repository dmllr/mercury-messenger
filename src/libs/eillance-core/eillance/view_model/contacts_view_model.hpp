#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ContactsViewModel {
public:
  explicit ContactsViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier);

  [[nodiscard]] auto GetContacts() const -> std::vector<ViewContact>;

  // Actions
  void AddContact(const ContactID& peer_id, const std::optional<std::string>& token = std::nullopt);
  void BlockContact(const ContactID& peer_id);
  void DeleteContact(const ContactID& peer_id);
  void UnblockContact(const ContactID& peer_id);
  void AcceptContact(const ContactID& peer_id);
  void SetContactDisplayNameOverride(const ContactID& peer_id, const std::string& name);
  void ResetContactDisplayNameOverride(const ContactID& peer_id);
  auto GenerateContactToken() -> std::string;

  // Internal updates called by MercuryViewModel
  void UpdateContactsList(bool notify = true);
  void UpdateContactConnectionState(const ContactID& peer_id, transport::PeerConnectionState state);

  // Debug
  void DebugEraseAllContacts();
  void DebugRevokeAllApprovals();

private:
  std::shared_ptr<Client> client_;
  std::function<void(StateChange)> notifier_;

  mutable std::mutex mutex_;
  std::vector<ViewContact> contacts_;
};

}  // namespace eillance::view_model
