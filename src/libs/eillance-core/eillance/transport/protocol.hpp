#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"

namespace eillance::transport::protocol {

namespace keys {
constexpr auto type = "type";
constexpr auto sdp = "sdp";
constexpr auto context = "context";
constexpr auto to = "to";
}  // namespace keys

namespace values {
constexpr auto offer = "offer";
constexpr auto answer = "answer";
constexpr auto prAnswer = "pranswer";

constexpr auto contextData = "data";
constexpr auto contextMedia = "media";
}  // namespace values

inline auto CreateSdp(std::string const& type, std::string const& sdp, std::string const& context, ContactID const& to)
    -> nlohmann::json {
  return {{keys::type, type}, {keys::sdp, sdp}, {keys::context, context}, {keys::to, to.ToString()}};
}

}  // namespace eillance::transport::protocol
