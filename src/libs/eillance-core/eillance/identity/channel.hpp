#pragma once

#include <string>
#include <vector>

#include "eillance/identity/channel_id.hpp"
#include "eillance/identity/contact_id.hpp"

namespace eillance::identity {

struct ChannelMember {
  ContactID contact_id;
  int role = 0;  // 0 = sub, 1 = admin
};

struct Channel {
  ChannelID id;
  std::string name;
  std::string description;
  ContactID owner_id;
  bool is_public = true;
  std::vector<ChannelMember> members;
};

}  // namespace eillance::identity
