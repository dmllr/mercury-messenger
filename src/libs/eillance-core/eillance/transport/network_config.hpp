#pragma once

#include <string>
#include <vector>

namespace eillance::transport {

[[nodiscard]] inline auto GetDefaultBootstrapNodeUrls() -> std::string {
  return "/dns4/jp-bn-101.eillance.com/tcp/10807\n"
         "/dns4/jp-bn-102.eillance.com/tcp/10807\n"
         "/dns4/us-bn-101.eillance.com/tcp/10807\n"
         "/dns4/us-bn-102.eillance.com/tcp/10807";
}

[[nodiscard]] inline auto ParseUrls(std::string const& input) -> std::vector<std::string> {
  std::vector<std::string> urls;
  std::string current;
  for (char const c : input) {
    if (c == '\n' || c == '\r' || c == ',' || c == ';') {
      if (!current.empty()) {
        urls.push_back(current);
        current.clear();
      }
    } else if (c != ' ') {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    urls.push_back(current);
  }
  return urls;
}

}  // namespace eillance::transport
