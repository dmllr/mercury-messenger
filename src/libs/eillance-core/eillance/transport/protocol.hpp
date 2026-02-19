#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "eillance/identity/contact_id.hpp"

namespace eillance::transport::protocol {

namespace keys {
constexpr auto type = "type";
constexpr auto id = "id";
constexpr auto to = "to";
constexpr auto sdp = "sdp";
constexpr auto context = "context";
constexpr auto signature = "signature";
constexpr auto challenge = "challenge";
constexpr auto servers = "servers";
constexpr auto error = "error";
constexpr auto forPeer = "for_peer";
}  // namespace keys

namespace values {
constexpr auto login = "login";
constexpr auto loginSuccess = "login_success";
constexpr auto authChallenge = "auth_challenge";
constexpr auto authResponse = "auth_response";
constexpr auto iceServers = "ice_servers";
constexpr auto offer = "offer";
constexpr auto answer = "answer";
constexpr auto prAnswer = "pranswer";
constexpr auto error = "error";
constexpr auto authFailed = "auth_failed";

constexpr auto contextData = "data";
constexpr auto contextMedia = "media";
}  // namespace values

inline auto CreateLogin(ContactID const& id) -> nlohmann::json {
  return {{keys::type, values::login}, {keys::id, id.ToString()}};
}

inline auto CreateAuthResponse(ContactID const& id, std::string const& signature) -> nlohmann::json {
  return {{keys::type, values::authResponse}, {keys::id, id.ToString()}, {keys::signature, signature}};
}

inline auto CreateSdp(std::string const& type, std::string const& sdp, std::string const& context, ContactID const& to)
    -> nlohmann::json {
  return {{keys::type, type}, {keys::sdp, sdp}, {keys::context, context}, {keys::to, to.ToString()}};
}

}  // namespace eillance::transport::protocol
