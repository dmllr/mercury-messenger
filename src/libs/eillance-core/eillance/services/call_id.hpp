#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>

namespace eillance::services {

struct CallID {
  static constexpr size_t callIdSize = 40;
  CallID() {
    value_[0] = '\0';
  }

  explicit CallID(std::string_view const str) {
    size_t const len = std::min(str.size(), callIdSize - 1);
    std::memcpy(value_.data(), str.data(), len);
    value_[len] = '\0';
  }

  [[nodiscard]] auto IsEmpty() const -> bool {
    return value_[0] == '\0';
  }

  [[nodiscard]] auto ToString() const -> std::string {
    return value_.data();
  }

  auto operator==(CallID const& other) const -> bool {
    return std::strcmp(value_.data(), other.value_.data()) == 0;
  }

 private:
  std::array<char, callIdSize> value_{};
};

}  // namespace eillance::services
