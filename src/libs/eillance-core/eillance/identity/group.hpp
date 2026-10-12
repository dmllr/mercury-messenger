#pragma once

#include <string>
#include <vector>

#include "eillance/identity/contact_id.hpp"
#include "eillance/identity/group_id.hpp"

namespace eillance::identity {

struct GroupMember {
  ContactID contact_id;
  int role = 0;  // 0 = member, 1 = owner
};

struct Group {
  GroupID id;
  std::string name;
  ContactID owner_id;
  std::vector<GroupMember> members;
};

}  // namespace eillance::identity
