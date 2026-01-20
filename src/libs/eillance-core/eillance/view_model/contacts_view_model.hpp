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
    Observer(const Observer&) = delete;
    Observer(Observer&&) = delete;
    auto operator=(const Observer&) -> Observer& = delete;
    auto operator=(Observer&&) -> Observer& = delete;
    virtual ~Observer() = default;
    virtual void OnContactsUpdated(const std::vector<ViewContact>& contacts) = 0;
  };

  explicit ContactsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~ContactsViewModel();

  void SetObserver(std::shared_ptr<Observer> observer);

  [[nodiscard]] auto GetContacts() const -> std::vector<ViewContact>;

  // Actions
  void AddContact(const ContactID& peerId, const std::optional<std::vector<std::byte>>& token);
  void BlockContact(const ContactID& peerId);
  void DeleteContact(const ContactID& peerId);
  void UnblockContact(const ContactID& peerId);
  void AcceptContact(const ContactID& peerId);
  void SetContactDisplayNameOverride(const ContactID& peerId, const std::string& name);
  void ResetContactDisplayNameOverride(const ContactID& peerId);
  auto GenerateContactToken() -> std::vector<std::byte>;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<std::function<void()>> eventUnsubscribers_;

  template<typename EventType>
  void Subscribe(EventQueue& queue, std::function<void(const EventType&)> callback);
};

}  // namespace eillance::view_model
