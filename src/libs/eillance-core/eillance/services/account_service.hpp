#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "eillance/backup/backup_manager.hpp"
#include "eillance/client/settings_manager.hpp"
#include "eillance/identity/id_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/transport.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class AccountService {
 public:
  explicit AccountService(
      std::shared_ptr<storage::PersistentStorage> storage,
      std::shared_ptr<transport::Transport> transport
  )
      : storage_(std::move(storage)),
        transport_(std::move(transport)) {
    if (auto db_path = storage_->GetPath(); !db_path.empty()) {
      backup_manager_ = std::make_unique<backup::BackupManager>(db_path);
    }

    id_manager_ = std::make_unique<identity::IdManager>(storage_);
    settings_manager_ = std::make_unique<SettingsManager>(storage_);
  }

  [[nodiscard]] auto GetId() const -> ContactID {
    return ContactID(id_manager_->GetPublicKey());
  }

  [[nodiscard]] auto IdentityExists() const -> bool {
    return id_manager_->IdentityExists();
  }

  [[nodiscard]] auto GetMnemonic() const -> std::optional<std::string> {
    return id_manager_->GetMnemonic();
  }

  // Returns new mnemonic
  [[nodiscard]] auto GenerateNewIdentity() const -> std::string {
    auto mnemonic = id_manager_->GenerateNewIdentity();
    if (!mnemonic.empty()) {
      INVARIANT(on_profile_updated_);
      on_profile_updated_();
    }
    return mnemonic;
  }

  [[nodiscard]] auto RestoreIdentity(std::string const& mnemonic) const -> bool {
    if (id_manager_->RestoreFromMnemonic(mnemonic)) {
      INVARIANT(on_profile_updated_);
      on_profile_updated_();
      return true;
    }
    return false;
  }

  [[nodiscard]] auto CreateBackup(std::string const& output_path, std::string const& password) const -> bool {
    INVARIANT(backup_manager_);
    return backup_manager_->CreateBackup(output_path, password);
  }

  auto RestoreBackup(std::string const& input_path, std::string const& password) -> bool {
    INVARIANT(backup_manager_);
    if (backup_manager_->RestoreFromBackup(input_path, password)) {
      // Reload settings and identity after database restore
      id_manager_ = std::make_unique<identity::IdManager>(storage_);
      settings_manager_ = std::make_unique<SettingsManager>(storage_);
      INVARIANT(on_profile_updated_);
      on_profile_updated_();
      return true;
    }
    return false;
  }

  // Settings & Profile
  [[nodiscard]] auto GetSettings() const -> SettingsManager* {
    return settings_manager_.get();
  }

  void UpdateMyProfile(
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  ) const {
    if (name) {
      settings_manager_->SetMyDisplayName(*name);
    }
    if (bio) {
      settings_manager_->SetMyBio(*bio);
    }
    if (photo) {
      settings_manager_->SetMyPhotoBase64(*photo);
    }

    INVARIANT(on_profile_updated_);
    on_profile_updated_();
  }

  // Updates the signaling URL in settings and triggers transport reconnect
  void UpdateSignalingUrl(std::string const& url) {
    settings_manager_->SetSignalingServerUrl(url);
    transport_->SetSignalingUrl(url);
    transport_->Reconnect();
  }

  // Signing utility using the managed identity
  [[nodiscard]] auto Sign(std::span<std::byte const> const message) const -> std::vector<std::byte> {
    return id_manager_->Sign(message);
  }

  // Callbacks
  using OnMyProfileUpdatedCallback = std::function<void()>;
  void SetOnMyProfileUpdatedCallback(OnMyProfileUpdatedCallback callback) {
    on_profile_updated_ = std::move(callback);
  }

 private:
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::Transport> transport_;

  std::unique_ptr<identity::IdManager> id_manager_;
  std::unique_ptr<SettingsManager> settings_manager_;
  std::unique_ptr<backup::BackupManager> backup_manager_;

  OnMyProfileUpdatedCallback on_profile_updated_;
};

}  // namespace eillance::services
