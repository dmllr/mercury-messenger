#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <algorithm>
#include <charconv>
#include <iostream>

struct ArgsParser final {
  ArgsParser(int &argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
      std::string token(argv[i]); // NOLINT(*-pro-bounds-pointer-arithmetic)
      auto pos = token.find('=');
      if (pos != std::string::npos) {
        std::string key = token.substr(0, pos);
        std::string value = token.substr(pos + 1);
        key_value_map_[key] = value;
      } else {
        tokens_.emplace_back(token);
      }
    }
  }

  [[nodiscard]] std::optional<std::string> Get(const std::string &option) const {
    auto it = key_value_map_.find(option);
    if (it != key_value_map_.end()) {
      return it->second;
    }
    auto itr = std::find(tokens_.begin(), tokens_.end(), option);
    if (itr != tokens_.end() && ++itr != tokens_.end()) { // NOLINT(*-inc-dec-in-conditions)
      return *itr;
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<std::string> GetPositional(size_t index) const {
    if (index < tokens_.size()) {
      return tokens_[index];
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<int> GetInt(const std::string &option) const {
    auto str_val = Get(option);
    if (!str_val) return std::nullopt;

    int value{};
    auto [ptr, ec] =
        // NOLINTNEXTLINE(*-narrowing-conversions)
        std::from_chars(str_val->data(), std::next(str_val->data(), str_val->size()), value);
    if (ec == std::errc{}) {
      return value;
    }
    return std::nullopt;
  }

  [[nodiscard]] bool Has(const std::string &option) const {
    return key_value_map_.contains(option)
           || std::find(tokens_.begin(), tokens_.end(), option) != tokens_.end();
  }

private:
  std::vector<std::string> tokens_;
  std::unordered_map<std::string, std::string> key_value_map_;
};
