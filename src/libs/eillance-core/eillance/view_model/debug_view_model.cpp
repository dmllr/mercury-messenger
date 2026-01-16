#include "debug_view_model.hpp"

namespace eillance::view_model {

DebugViewModel::DebugViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
    : client_(std::move(client)), router_(std::move(router)) {}

void DebugViewModel::EraseAllHistory() {
  client_->DebugEraseAllHistory();
  router_->Queue().Push(EventDebugDataReset{});
}

void DebugViewModel::EraseAllContacts() {
  client_->DebugEraseAllContacts();
  router_->Queue().Push(EventDebugDataReset{});
}

void DebugViewModel::RevokeAllApprovals() {
  client_->DebugRevokeAllApprovals();
  router_->Queue().Push(EventDebugDataReset{});
}

}  // namespace eillance::view_model
