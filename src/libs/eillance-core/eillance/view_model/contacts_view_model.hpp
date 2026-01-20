#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class ContactsViewModel {
 public:
  // Observer interface for contact list updates
  struct Observer {
    Observer() = default;
    Observer(Observer const&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(Observer const&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnContactsUpdated(std::vector<ViewContact> const& contacts) = 0;
  };

  explicit ContactsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~ContactsViewModel();

  void SetObserver(std::shared_ptr<Observer> observer) const;

  [[nodiscard]] auto GetContacts() const -> std::vector<ViewContact>;

  // Actions
  void AddContact(ContactID const& peerId, std::optional<std::vector<std::byte>> const& token) const;
  void BlockContact(ContactID const& peerId);
  void DeleteContact(ContactID const& peerId);
  void UnblockContact(ContactID const& peerId);
  void AcceptContact(ContactID const& peerId);
  void SetContactDisplayNameOverride(ContactID const& peerId, std::string const& name);
  void ResetContactDisplayNameOverride(ContactID const& peerId);
  auto GenerateContactToken() -> std::vector<std::byte>;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<std::function<void()>> eventUnsubscribers_;

  template <typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(EventType const&)> const& callback);
};

}  // namespace eillance::view_model
