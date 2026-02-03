#pragma once

#include <memory>
#include <vector>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class DebugViewModel {
public:
  explicit DebugViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router)
      : client_(std::move(client)),
        router_(std::move(router)) {
  }

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

  [[nodiscard]] auto GetPendingActions() const -> std::vector<DebugPendingAction> {
    return client_->GetAllPendingActions();
  }

  [[nodiscard]] auto GetConnectionInfo() const -> std::vector<DebugConnectionInfo> {
    return client_->GetConnectionDebugInfo();
  }

  [[nodiscard]] auto GetUnreadInfo() const -> std::vector<DebugUnreadInfo> {
    return client_->GetAllUnreadInfo();
  }

private:
  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
};

}  // namespace eillance::view_model
