#pragma once

#include <cassert>
#include <climits>
#include <cstdint>

#include <rtc/rtc.hpp>

namespace eillance {

/// @brief A simple bitstream reader for SPS parsing.
class BitReader {
 public:
  BitReader(std::uint8_t const* data, std::uint32_t const size)
      : data_{data},
        size_{size},
        byteOffset_{0},
        bitOffset_{0} {}

  // Read a single bit (returns in 'bit').
  bool ReadBit(std::uint32_t& bit) {
    if (byteOffset_ >= size_) {
      return false;
    }
    bit = (data_[byteOffset_] >> (7U - bitOffset_)) & 0x01U;
    bitOffset_++;
    if (bitOffset_ == CHAR_BIT) {
      bitOffset_ = 0;
      byteOffset_++;
    }
    return true;
  }

  // Read n bits and return as an unsigned integer.
  std::uint32_t ReadBits(std::uint32_t const n) {
    assert(n < 8 * sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    for (std::uint32_t i = 0U; i < n; i++) {
      std::uint32_t bit = 0;
      if (!ReadBit(bit)) {
        break;
      }
      bits = (bits << 1U) | bit;
    }
    return bits;
  }

  // Read byte and return as an unsigned integer.
  std::uint32_t ReadByte() { return ReadBits(CHAR_BIT); }

  // Read an unsigned Exp-Golomb coded value.
  std::uint32_t ReadUE() {
    std::uint32_t zeros = 0;
    std::uint32_t bit = 0;

    // Count leading zeros
    while (ReadBit(bit) && bit == 0) {
      zeros++;
    }

    // Read the remaining bits after the leading 0s
    auto const rest = ReadBits(zeros);

    // Compute Exp-Golomb value
    return (1U << zeros) - 1 + rest;
  }

  // Read a signed Exp-Golomb coded value.
  std::int32_t ReadSE() {
    auto const code_num = ReadUE();
    return ((code_num & 1U) != 0U) ? static_cast<int32_t>((code_num + 1) >> 1U) : -static_cast<int32_t>(code_num >> 1U);
  }

 private:
  std::uint8_t const* data_;
  std::uint32_t size_;
  std::uint32_t byteOffset_;
  std::uint8_t bitOffset_;
};

}  // namespace eillance
