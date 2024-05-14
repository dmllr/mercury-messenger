#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include <rtc/common.hpp>

#include "hela/eillance/utils/time.hpp"


namespace hela::eillance {

class Packet {
  rtc::binary buffer_{};
  Time::Unit timestamp_{Time::Now()};

public:
  Packet() = default;

  /// construct from rtc::binary pointer and size
  explicit Packet(rtc::binary&& buffer) : buffer_(std::move(buffer)) {
  }

  /// construct from std::byte pointer and size
  Packet(const std::byte* data, std::uint32_t data_size) {
    buffer_.insert(buffer_.begin(), data, std::next(data, data_size));
  }

  /// construct from two std::byte pointers
  Packet(const std::byte* data_begin, const std::byte* data_end) {
    buffer_.insert(buffer_.begin(), data_begin, data_end);
  }

  /// construct from uchar pointer and size
  Packet(const unsigned char* data, std::uint32_t data_size) {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    auto* bytes = reinterpret_cast<const std::byte*>(data);

    buffer_.insert(buffer_.begin(), bytes, std::next(bytes, data_size));
  }

  /// construct from two uchar pointers
  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  Packet(const unsigned char* data_begin, const unsigned char* data_end) {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    auto* bytes_begin = reinterpret_cast<const std::byte*>(data_begin);
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    auto* bytes_end = reinterpret_cast<const std::byte*>(data_end);

    buffer_.insert(buffer_.begin(), bytes_begin, bytes_end);
  }

  /// construct from std::vector
  explicit Packet(const std::vector<std::byte>& data) : buffer_(data) {
  }

  // begin method to return the iterator to the beginning of the buffer
  [[nodiscard]] auto begin() {
    return buffer_.begin();
  }

  // const begin method to return the const iterator to the beginning of the buffer
  [[nodiscard]] auto begin() const {
    return buffer_.begin();
  }

  // end method to return the iterator to the end of the buffer
  [[nodiscard]] auto end() {
    return buffer_.end();
  }

  // const end method to return the const iterator to the end of the buffer
  [[nodiscard]] auto end() const {
    return buffer_.end();
  }

  void append(const Packet& other) {
    buffer_.insert(buffer_.end(), other.buffer_.begin(), other.buffer_.end());
    timestamp_ = other.timestamp_;
  }

  [[nodiscard]] auto buffer() {
    return buffer_;
  }

  [[nodiscard]] auto data() const {
    return buffer_.data();
  }

  [[maybe_unused]] [[nodiscard]] auto dataAsChars() const {
    return reinterpret_cast<const char*>(buffer_.data());
  }

  [[maybe_unused]] [[nodiscard]] auto dataAsUChars() const {
    return reinterpret_cast<const unsigned char*>(buffer_.data());
  }

  [[nodiscard]] auto timestamp() const {
    return timestamp_;
  }

  [[nodiscard]] auto empty() const {
    return buffer_.empty();
  }

  [[nodiscard]] auto size() const {
    return buffer_.size();
  }

  void clear() {
    buffer_.clear();
  }
};

}  // namespace hela::eillance
