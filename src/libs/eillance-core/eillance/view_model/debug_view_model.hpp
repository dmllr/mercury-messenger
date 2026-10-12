#pragma once

#include <memory>
#include <vector>

#include "eillance/client/client.hpp"
#include "eillance/events/client_events.hpp"
#include "eillance/utils/event_queue.hpp"
#include "view_model_defines.hpp"

namespace eillance::view_model {

class DebugViewModel {
 public:
  explicit DebugViewModel(std::shared_ptr<Client> client)
      : client_(std::move(client)) {
  }

  void EraseAllHistory() {
    client_->DebugEraseAllHistory();
    EventQueue::Instance().Push(EventDebugDataReset{});
  }

  void EraseAllContacts() {
    client_->DebugEraseAllContacts();
    EventQueue::Instance().Push(EventDebugDataReset{});
  }

  void RevokeAllApprovals() {
    client_->DebugRevokeAllContactApprovals();
    EventQueue::Instance().Push(EventDebugDataReset{});
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
};

}  // namespace eillance::view_model
