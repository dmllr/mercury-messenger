#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace eillance::transport {

struct ParsedTurnServer {
  std::string url;
  std::string username;
  std::string credential;
};

[[nodiscard]] auto GetDefaultBootstrapNodeUrls() -> std::string;
[[nodiscard]] auto ParseUrls(std::string_view input) -> std::vector<std::string>;
[[nodiscard]] auto ParseTurnServers(std::string_view input) -> std::vector<ParsedTurnServer>;

}  // namespace eillance::transport
