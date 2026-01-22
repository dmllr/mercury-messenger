#pragma once

#include <memory>
#include <optional>
#include <string>

#include "eillance/backup/backup_manager.hpp"
#include "eillance/client/settings_manager.hpp"
#include "eillance/identity/id_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"

namespace eillance::services {

class AccountService {
 public:
  explicit AccountService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport
  );

  [[nodiscard]] auto GetId() const -> ContactID;
  [[nodiscard]] auto IdentityExists() const -> bool;
  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string>;

  // Returns new mnemonic
  auto GenerateNewIdentity() const -> std::string;
  auto RestoreIdentity(std::string const& mnemonic) const -> bool;

  auto CreateBackup(std::string const& output_path, std::string const& password) const -> bool;
  auto RestoreBackup(std::string const& input_path, std::string const& password) -> bool;

  // Settings & Profile
  [[nodiscard]] auto GetSettings() const -> SettingsManager*;

  void UpdateMyProfile(
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  ) const;

  // Updates the signaling URL in settings and triggers transport reconnect
  void UpdateSignalingUrl(std::string const& url) const;

  // Signing utility using the managed identity
  [[nodiscard]] auto Sign(std::span<std::byte const> message) const -> std::vector<std::byte>;

  // Callbacks
  using OnMyProfileUpdatedCallback = std::function<void()>;
  void SetOnMyProfileUpdatedCallback(OnMyProfileUpdatedCallback callback);

 private:
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;

  std::unique_ptr<identity::IdManager> id_manager_;
  std::unique_ptr<SettingsManager> settings_manager_;
  std::unique_ptr<backup::BackupManager> backup_manager_;

  OnMyProfileUpdatedCallback on_profile_updated_;
};

}  // namespace eillance::services
