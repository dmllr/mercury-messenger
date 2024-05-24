#pragma once

#include <cstdlib>
#include <ctime>
#include <string>

#include "hela/eillance/utils/time.hpp"

class StringUtils {
public:
  static std::string RandomString() {
    constexpr auto len = 10;
    constexpr auto char_set_len = 62;
    constexpr auto char_set = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    std::srand(Time::Now());
    auto random_string{std::string(len, '\0')};
    for (int i = 0; i < len; ++i) {
      const auto random_poz = std::rand() % char_set_len;
      random_string[i] = char_set[random_poz];
    }

    return random_string;
  }
};
