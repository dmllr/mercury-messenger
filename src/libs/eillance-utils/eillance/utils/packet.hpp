#pragma once

#include <cstdint>
#include <vector>

#include "eillance/utils/time.hpp"

namespace eillance {

class Packet {
  using Binary = std::vector<std::byte>;

  Binary buffer_{};
  Time::Unit timestamp_{Time::Now()};

 public:
  Packet() = default;

  /// construct from Binary pointer and size
  explicit Packet(Binary&& buffer)
      : buffer_(std::move(buffer)) {}

  /// construct from std::byte pointer and size
  Packet(std::byte const* data, std::uint32_t data_size) {
    buffer_.insert(buffer_.begin(), data, std::next(data, data_size));
  }

  /// construct from two std::byte pointers
  Packet(std::byte const* data_begin, std::byte const* data_end) {
    buffer_.insert(buffer_.begin(), data_begin, data_end);
  }

  /// construct from uchar pointer and size
  Packet(unsigned char const* data, std::uint32_t data_size) {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    auto* bytes = reinterpret_cast<std::byte const*>(data);

    buffer_.insert(buffer_.begin(), bytes, std::next(bytes, data_size));
  }

  /// construct from two uchar pointers
  // NOLINTNEXTLINE(*-easily-swappable-parameters)
  Packet(unsigned char const* data_begin, unsigned char const* data_end) {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    auto* bytes_begin = reinterpret_cast<std::byte const*>(data_begin);
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    auto* bytes_end = reinterpret_cast<std::byte const*>(data_end);

    buffer_.insert(buffer_.begin(), bytes_begin, bytes_end);
  }

  /// construct from std::vector
  explicit Packet(std::vector<std::byte> const& data)
      : buffer_(data) {}

  // begin method to return the iterator to the beginning of the buffer
  [[nodiscard]] auto begin() {  // NOLINT(*-identifier-naming)
    return buffer_.begin();
  }

  // const begin method to return the const iterator to the beginning of the buffer
  [[nodiscard]] auto begin() const {  // NOLINT(*-identifier-naming)
    return buffer_.begin();
  }

  // end method to return the iterator to the end of the buffer
  [[nodiscard]] auto end() {  // NOLINT(*-identifier-naming)
    return buffer_.end();
  }

  // const end method to return the const iterator to the end of the buffer
  [[nodiscard]] auto end() const {  // NOLINT(*-identifier-naming)
    return buffer_.end();
  }

  void append(Packet const& other) {  // NOLINT(*-identifier-naming)
    buffer_.insert(buffer_.end(), other.buffer_.begin(), other.buffer_.end());
    timestamp_ = other.timestamp_;
  }

  [[nodiscard]] auto buffer() {  // NOLINT(*-identifier-naming)
    return buffer_;
  }

  [[nodiscard]] auto data() const {  // NOLINT(*-identifier-naming)
    return buffer_.data();
  }

  [[maybe_unused]] [[nodiscard]] auto dataAsChars() const {  // NOLINT(*-identifier-naming)
    return reinterpret_cast<char const*>(buffer_.data());    // NOLINT(*-pro-type-reinterpret-cast)
  }

  [[maybe_unused]] [[nodiscard]] auto dataAsUChars() const {        // NOLINT(*-identifier-naming)
    return reinterpret_cast<unsigned char const*>(buffer_.data());  // NOLINT(*-pro-type-reinterpret-cast)
  }

  [[nodiscard]] auto timestamp() const {  // NOLINT(*-identifier-naming)
    return timestamp_;
  }

  [[nodiscard]] auto empty() const {  // NOLINT(*-identifier-naming)
    return buffer_.empty();
  }

  [[nodiscard]] auto size() const {  // NOLINT(*-identifier-naming)
    return buffer_.size();
  }

  void clear() {  // NOLINT(*-identifier-naming)
    buffer_.clear();
  }
};

}  // namespace eillance
