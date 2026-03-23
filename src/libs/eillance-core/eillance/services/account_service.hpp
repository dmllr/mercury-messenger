#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "eillance/backup/backup_manager.hpp"
#include "eillance/client/settings_manager.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/identity/id_manager.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/storage/persistent_storage.hpp"
#include "eillance/transport/data_transport.hpp"
#include "eillance/utils/event_queue.hpp"
#include "eillance/utils/invariant.hpp"

namespace eillance::services {

class AccountService {
 public:
  explicit AccountService(ServiceManager* sm)
      : sm_(sm),
        storage_(sm->storage),
        transport_(sm->data_transport) {
    if (auto db_path = sm_->storage->GetPath(); !db_path.empty()) {
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
      EventQueue::Instance().Push(EventMyProfileUpdated{});
    }
    return mnemonic;
  }

  [[nodiscard]] auto RestoreIdentity(std::string const& mnemonic) const -> bool {
    if (id_manager_->RestoreFromMnemonic(mnemonic)) {
      EventQueue::Instance().Push(EventMyProfileUpdated{});
      return true;
    }
    return false;
  }

  [[nodiscard]] auto CreateBackup(std::string const& output_path, std::string const& password) const -> bool {
    INVARIANT(backup_manager_);
    // Ensure all data in WAL is flushed to the main DB file before we read it as a binary file.
    storage_->Checkpoint();
    return backup_manager_->CreateBackup(output_path, password);
  }

  auto RestoreBackup(std::string const& input_path, std::string const& password) -> bool {
    INVARIANT(backup_manager_);

    // Close storage to release file locks and ensure SQLite sees the new file content upon reopen
    storage_->Close();

    // Delete WAL and SHM files to prevent SQLite from confusing the new DB file with old journal files
    std::error_code ec;
    auto const db_path = storage_->GetPath();
    std::filesystem::remove(db_path + "-wal", ec);
    std::filesystem::remove(db_path + "-shm", ec);
    bool const restore_success = backup_manager_->RestoreFromBackup(input_path, password);

    // Re-open the storage (this will load the new DB file if restore succeeded, or the old one if failed/untouched)
    if (!storage_->Open()) {
      LGE << "Failed to reopen storage after backup restore attempt.";
      return false;
    }

    if (restore_success) {
      // Reload settings and identity after database restore
      id_manager_ = std::make_unique<identity::IdManager>(storage_);
      settings_manager_ = std::make_unique<SettingsManager>(storage_);

      // Verify the restored DB actually contains a valid identity
      if (!id_manager_->IdentityExists()) {
        LGE << "Restored backup does not contain a valid identity.";
        return false;
      }

      EventQueue::Instance().Push(EventMyProfileUpdated{});
      return true;
    }
    return false;
  }

  // Settings & Profile
  [[nodiscard]] auto GetSettings() const -> SettingsManager* {
    return settings_manager_.get();
  }

  void UpdateMyProfile(identity::ProfileUpdate const& profile) const {
    if (profile.display_name) {
      settings_manager_->SetMyDisplayName(*profile.display_name);
    }
    if (profile.bio) {
      settings_manager_->SetMyBio(*profile.bio);
    }
    if (profile.photo_base64) {
      settings_manager_->SetMyPhotoBase64(*profile.photo_base64);
    }

    EventQueue::Instance().Push(EventMyProfileUpdated{});
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

  void UpdatePlatformPermissions(bool has_notif, bool has_battery, bool has_calls) {
    has_notif_ = has_notif;
    has_battery_ = has_battery;
    has_calls_ = has_calls;
    EvaluatePermissionBanner();
  }

  void IgnoreBatteryOptimization() {
    settings_manager_->SetIgnoreBatteryOptimization(true);
    EvaluatePermissionBanner();
  }

  void IgnoreCallPermissions() {
    settings_manager_->SetIgnoreCallPermissions(true);
    EvaluatePermissionBanner();
  }

  void ResetIgnoredPermissions() {
    settings_manager_->SetIgnoreBatteryOptimization(false);
    settings_manager_->SetIgnoreCallPermissions(false);
    EvaluatePermissionBanner();
  }

 private:
  void EvaluatePermissionBanner() {
    PermissionBannerType banner = PermissionBannerType::none;
    if (!has_notif_)
      banner = PermissionBannerType::notification;
    else if (!has_battery_ && !settings_manager_->GetIgnoreBatteryOptimization())
      banner = PermissionBannerType::battery;
    else if (!has_calls_ && !settings_manager_->GetIgnoreCallPermissions())
      banner = PermissionBannerType::callPermissions;
    EventQueue::Instance().Push(EventPermissionBannerChanged{banner});
  }

  ServiceManager* sm_;
  std::shared_ptr<storage::PersistentStorage> storage_;
  std::shared_ptr<transport::DataTransport> transport_;

  std::unique_ptr<identity::IdManager> id_manager_;
  std::unique_ptr<SettingsManager> settings_manager_;
  std::unique_ptr<backup::BackupManager> backup_manager_;

  bool has_notif_{true};
  bool has_battery_{true};
  bool has_calls_{true};
};

}  // namespace eillance::services
