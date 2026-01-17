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
    virtual ~Observer() = default;
    virtual void OnContactsUpdated(const std::vector<ViewContact>& contacts) = 0;
  };

  explicit ContactsViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);
  ~ContactsViewModel();

  void SetObserver(std::shared_ptr<Observer> observer);

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

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;

  // Unsubscribers must be kept in the wrapper to handle event deregistration on destruction
  std::vector<std::function<void()>> event_unsubscribers_;

  template<typename EventType>
  void Subscribe(EventQueue& q, std::function<void(const EventType&)> callback);
};

}  // namespace eillance::view_model
