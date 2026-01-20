#pragma once

#include <memory>

#include "client_event_router.hpp"
#include "eillance/client/client.hpp"

namespace eillance::view_model {

class DebugViewModel {
 public:
  explicit DebugViewModel(std::shared_ptr<Client> client, std::shared_ptr<ClientEventRouter> router);

  void EraseAllHistory();
  void EraseAllContacts();
  void RevokeAllApprovals();

 private:
  std::shared_ptr<Client> client_;
  std::shared_ptr<ClientEventRouter> router_;
};

}  // namespace eillance::view_model
