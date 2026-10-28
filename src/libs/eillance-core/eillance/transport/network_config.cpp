#include "eillance/transport/network_config.hpp"

#include "eillance/transport/network_parser.hpp"

namespace eillance::transport {

auto GetDefaultBootstrapNodeUrls() -> std::string {
  return "/dns4/jp-bn-101.eillance.com/tcp/10807\n"
         "/dns4/jp-bn-102.eillance.com/tcp/10807\n"
         "/dns4/us-bn-101.eillance.com/tcp/10807\n"
         "/dns4/us-bn-102.eillance.com/tcp/10807";
}

auto ParseUrls(std::string_view const input) -> std::vector<std::string> {
  std::vector<std::string> urls;
  std::string current;
  auto const process_item = [&](std::string_view const item) {
    if (auto const trimmed = Trim(item); !trimmed.empty() && ValidateBootstrapUrl(trimmed)) {
      urls.emplace_back(trimmed);
    }
  };

  for (char const c : input) {
    if (c == '\n' || c == '\r' || c == ',' || c == ';') {
      if (!current.empty()) {
        process_item(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }

  if (!current.empty()) {
    process_item(current);
  }

  return urls;
}

auto ParseTurnServers(std::string_view const input) -> std::vector<ParsedTurnServer> {
  std::vector<ParsedTurnServer> result;
  std::string current;
  auto const process_item = [&](std::string_view const item) {
    if (auto const trimmed = Trim(item); !trimmed.empty()) {
      if (auto parsed = ParseTurnServerEntry(trimmed)) {
        result.push_back(std::move(*parsed));
      }
    }
  };

  for (char const c : input) {
    if (c == '\n' || c == '\r' || c == ',' || c == ';') {
      if (!current.empty()) {
        process_item(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }

  if (!current.empty()) {
    process_item(current);
  }

  return result;
}

}  // namespace eillance::transport
