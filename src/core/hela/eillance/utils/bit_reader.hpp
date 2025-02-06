#pragma once

#include <string>
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include <rtc/rtc.hpp>

namespace hela::eillance {

/// @brief A simple bitstream reader for SPS parsing.
class BitReader {
public:
  BitReader(const std::uint8_t* data, std::size_t size)
      : data_(data), size_(size), byte_offset_(0), bit_offset_(0) {}

  // Read a single bit (returns in 'bit').
  bool ReadBit(std::uint32_t& bit) {
    if (byte_offset_ >= size_) {
      return false;
    }
    bit = (data_[byte_offset_] >> (7U - bit_offset_)) & 0x01U;
    bit_offset_++;
    if (bit_offset_ == 8) {
      bit_offset_ = 0;
      byte_offset_++;
    }
    return true;
  }

  // Read n bits and return as an unsigned integer.
  std::uint32_t ReadBits(std::uint32_t n) {
    assert(n < 8 * sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    for (int i = 0; i < n; i++) {
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
    std::uint32_t rest = ReadBits(zeros);

    // Compute Exp-Golomb value
    return (1U << zeros) - 1 + rest;
  }

  // Read a signed Exp-Golomb coded value.
  std::int32_t ReadSE() {
    std::uint32_t code_num = ReadUE();
    return ((code_num & 1U) != 0U)
               ? static_cast<int32_t>((code_num + 1) >> 1U)
               : -static_cast<int32_t>(code_num >> 1U);
  }

private:
  const std::uint8_t* data_;
  std::uint32_t size_;
  std::uint32_t byte_offset_;
  std::uint8_t bit_offset_;
};

}  // namespace hela::eillance
