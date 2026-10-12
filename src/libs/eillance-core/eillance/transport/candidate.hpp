#pragma once

#include <string>

namespace eillance::transport {

struct Candidate {
  std::string sdp;
  std::string mid;
};

}  // namespace eillance::transport
