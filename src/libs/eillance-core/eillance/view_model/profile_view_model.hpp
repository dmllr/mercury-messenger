#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ProfileViewModel {
public:
  explicit ProfileViewModel(std::shared_ptr<Client> client, std::function<void(StateChange)> notifier);

  [[nodiscard]] auto GetProfile() const -> MyProfile;

  // Actions
  void UpdateProfile(const std::optional<std::string>& name,
                     const std::optional<std::string>& bio,
                     const std::optional<std::string>& photo);

  // Creation/Restore
  auto GenerateNewIdentity() -> std::string;
  auto RestoreIdentity(const std::string& mnemonic) -> bool;
  auto GetMnemonic() -> std::optional<std::string>;
  auto CreateBackup(const std::string& path, const std::string& password) -> bool;
  auto RestoreBackup(const std::string& path, const std::string& password) -> bool;

  // Internal update
  void RefreshFromSettings();

private:
  std::shared_ptr<Client> client_;
  std::function<void(StateChange)> notifier_;

  mutable std::mutex mutex_;
  MyProfile profile_;
};

}  // namespace eillance::view_model
