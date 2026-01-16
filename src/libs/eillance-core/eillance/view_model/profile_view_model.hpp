#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ProfileViewModel {
public:
  struct Observer {
    virtual ~Observer() = default;
    virtual void OnProfileUpdated(const MyProfile& profile) = 0;
  };

  explicit ProfileViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~ProfileViewModel();

  void SetObserver(std::shared_ptr<Observer> observer);

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

private:
  void RefreshFromSettings();

  template<typename EventType>
  void Subscribe(EventQueue& q, std::function<void(const EventType&)> callback);

  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
  std::shared_ptr<Observer> observer_;

  mutable std::mutex mutex_;
  MyProfile profile_;
  std::vector<std::function<void()>> event_unsubscribers_;
};

}  // namespace eillance::view_model
