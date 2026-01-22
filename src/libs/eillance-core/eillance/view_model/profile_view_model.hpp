#pragma once

#include <functional>
#include <memory>
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
    Observer() = default;
    Observer(Observer const&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(Observer const&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnProfileUpdated(MyProfile const& profile) = 0;
  };

  explicit ProfileViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~ProfileViewModel();

  void SetObserver(std::shared_ptr<Observer> observer) const;

  [[nodiscard]] auto GetProfile() const -> MyProfile;

  // Actions
  void UpdateProfile(
      std::optional<std::string> const& name,
      std::optional<std::string> const& bio,
      std::optional<std::string> const& photo
  );

  // Creation/Restore
  auto GenerateNewIdentity() -> std::string;
  auto RestoreIdentity(std::string const& mnemonic) -> bool;
  [[nodiscard]] auto GetMnemonic() -> std::optional<std::string>;
  auto CreateBackup(std::string const& path, std::string const& password) -> bool;
  auto RestoreBackup(std::string const& path, std::string const& password) -> bool;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<std::function<void()>> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback);
};

}  // namespace eillance::view_model
