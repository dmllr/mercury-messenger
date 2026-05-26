#pragma once

#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/events/client_events.hpp"
#include "eillance/logging/logging.hpp"
#include "eillance/services/contact_service.hpp"
#include "eillance/services/messaging_service.hpp"
#include "eillance/services/profile_service.hpp"
#include "eillance/services/service_manager.hpp"
#include "eillance/utils/event_queue.hpp"

namespace eillance::services {

class LedgerProjectionService {
  using Json = nlohmann::json;

 public:
  explicit LedgerProjectionService(ServiceManager* sm)
      : sm_(sm) {
    event_unsubs_.push_back(EventQueue::Instance().Subscribe<EventLedgerAppended>([this](auto const& e) {
      this->ProjectEntry(e.entry);
    }));
  }

  ~LedgerProjectionService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

 private:
  void ProjectEntry(messaging::LedgerEntry const& entry) {
    bool const is_mine = (entry.author_id == sm_->account->GetId());

    try {
      auto const json = Json::parse(entry.payload);

      switch (entry.type) {
        case messaging::LedgerActionType::Message:
          sm_->messaging->ProjectMessage(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::MessageEdit:
          sm_->messaging->ProjectEdit(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::MessageDelete:
          sm_->messaging->ProjectDelete(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::Reaction:
          sm_->messaging->ProjectReaction(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::CallLog:
          sm_->messaging->ProjectCallLog(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::ProfileUpdate:
          sm_->profile->ProjectProfileUpdate(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::ContactState:
          sm_->contact->ProjectContactState(entry, json, is_mine);
          break;
      }
    } catch (std::exception const& e) {
      LGE << "Failed to parse and project ledger entry: " << e.what();
    }
  }

  ServiceManager* sm_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;
};

}  // namespace eillance::services
