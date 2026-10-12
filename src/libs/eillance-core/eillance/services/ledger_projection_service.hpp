#pragma once

#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "eillance/events/client_events.hpp"
#include "eillance/identity/group_manager.hpp"
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
      this->ProjectEntry(e.entry, e.is_synced);
    }));
  }

  ~LedgerProjectionService() {
    for (auto const id : event_unsubs_) {
      EventQueue::Instance().Unsubscribe(id);
    }
  }

 private:
  void ProjectEntry(messaging::LedgerEntry const& entry, bool is_synced) {
    bool const is_mine = (entry.author_id == sm_->account->GetId());

    if (is_mine && !is_synced) {
      return;
    }

    try {
      auto const json = Json::parse(entry.payload);

      switch (entry.type) {
        case messaging::LedgerActionType::Message:
          sm_->messaging->ProjectMessage(entry, json, is_mine, is_synced);
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
          sm_->messaging->ProjectCallLog(entry, json, is_mine, is_synced);
          break;
        case messaging::LedgerActionType::ProfileUpdate:
          sm_->profile->ProjectProfileUpdate(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::ContactState:
          sm_->contact->ProjectContactState(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::GroupState:
          sm_->group_manager->ProjectGroupState(entry, json, is_mine);
          break;
        case messaging::LedgerActionType::ChannelState: {
          if (is_mine) break;
          std::string const type = json.value("type", "");
          if (type == "channel_create") {
            identity::Channel c;
            c.id = ChannelID(entry.topic_id.Span());
            c.name = json.value("name", "");
            c.description = json.value("description", "");
            c.owner_id = ContactID(json.value("owner_id", ""));
            sm_->channel_manager->CreateChannelLocal(c);
            EventQueue::Instance().Push(EventGroupsStateUpdated{});
          } else if (type == "channel_subscribe" || type == "channel_member_add") {
            auto c = sm_->channel_manager->GetChannel(ChannelID(entry.topic_id.Span()));
            if (c) {
              sm_->channel_manager
                  ->AddMemberLocal(c->id, ContactID(json.value("contact_id", "")), json.value("role", 0));
              EventQueue::Instance().Push(EventGroupsStateUpdated{});
            }
          } else if (type == "channel_unsubscribe" || type == "channel_member_remove") {
            auto c = sm_->channel_manager->GetChannel(ChannelID(entry.topic_id.Span()));
            ContactID target(json.value("contact_id", ""));
            if (c) {
              if (type == "channel_unsubscribe" && target != entry.author_id) break;  // Can only unsubscribe self
              if (type == "channel_member_remove" && c->owner_id != entry.author_id)
                break;  // Only owner can remove others
              sm_->channel_manager->RemoveMemberLocal(c->id, target);
              EventQueue::Instance().Push(EventGroupsStateUpdated{});
            }
          }
          break;
        }
      }
    } catch (std::exception const& e) {
      LGE << "Failed to parse and project ledger entry: " << e.what();
    }
  }

  ServiceManager* sm_;
  std::vector<EventQueue::SubscriberTypeId> event_unsubs_;
};

}  // namespace eillance::services
