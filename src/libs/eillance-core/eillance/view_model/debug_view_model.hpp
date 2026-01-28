#pragma once

#include <memory>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"

namespace eillance::view_model {

class DebugViewModel {
 public:
  explicit DebugViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : client_(std::move(client)),
        router_(std::move(router)) {}

  void EraseAllHistory() {
    client_->DebugEraseAllHistory();
    router_->Queue().Push(EventDebugDataReset{});
  }

  void EraseAllContacts() {
    client_->DebugEraseAllContacts();
    router_->Queue().Push(EventDebugDataReset{});
  }

  void RevokeAllApprovals() {
    client_->DebugRevokeAllContactApprovals();
    router_->Queue().Push(EventDebugDataReset{});
  }

 private:
  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
};

}  // namespace eillance::view_model
