#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <rtc/rtc.hpp>

namespace eillance {

/// @brief A simple bitstream reader for SPS parsing.
class BitReader {
public:
  BitReader(const std::uint8_t* data, const std::uint32_t size)
      : data_(data), size_(size), byteOffset_(0), bitOffset_(0) {}

  // Read a single bit (returns in 'bit').
  bool ReadBit(std::uint32_t& bit) {
    if (byteOffset_ >= size_) {
      return false;
    }
    bit = (data_[byteOffset_] >> (7U - bitOffset_)) & 0x01U;
    bitOffset_++;
    if (bitOffset_ == 8) {
      bitOffset_ = 0;
      byteOffset_++;
    }
    return true;
  }

  // Read n bits and return as an unsigned integer.
  std::uint32_t ReadBits(const std::uint32_t n) {
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
  std::uint32_t ReadByte() {
    return ReadBits(8);
  }

  // Read an unsigned Exp-Golomb coded value.
  std::uint32_t ReadUE() {
    std::uint32_t zeros = 0;
    std::uint32_t bit = 0;

    // Count leading zeros
    while (ReadBit(bit) && bit == 0) {
      zeros++;
    }

    // Read the remaining bits after the leading 0s
    const auto rest = ReadBits(zeros);

    // Compute Exp-Golomb value
    return (1U << zeros) - 1 + rest;
  }

  // Read a signed Exp-Golomb coded value.
  std::int32_t ReadSE() {
    const auto code_num = ReadUE();
    return ((code_num & 1U) != 0U)
               ? static_cast<int32_t>((code_num + 1) >> 1U)
               : -static_cast<int32_t>(code_num >> 1U);
  }

private:
  const std::uint8_t* data_;
  std::uint32_t size_;
  std::uint32_t byteOffset_;
  std::uint8_t bitOffset_;
};

}  // namespace eillance
