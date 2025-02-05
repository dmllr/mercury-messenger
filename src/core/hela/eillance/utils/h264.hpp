#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <cassert>
#include <concepts>

#include <rtc/rtc.hpp>
#include "hela/eillance/utils/bit_reader.hpp"

/// @brief H.264 NAL unit parser.
/// @details This class provides a platform-agnostic H.264 NAL unit parser that
/// can process incoming frames that may contain multiple NAL units in a
/// single input buffer. It extracts SPS/PPS information (from separate
/// NALUs) to trigger codec configuration and then reassembles non-SPS/PPS
/// NALUs into a frame buffer.
class H264Parser {
public:
  /// @brief Callback type for codec configuration.
  /// @param sps Sequence Parameter Set (SPS) data.
  /// @param pps Picture Parameter Set (PPS) data.
  using ConfigureCallback = std::function<void(const std::vector<std::byte>& sps,
                                               const std::vector<std::byte>& pps)>;

  /// @brief Callback type for handling input buffer.
  /// The frame buffer contains a series of NAL units where each NALU is preceded by a
  /// 4‑byte big‑endian size header.
  /// @param frameBuffer The frame buffer containing a series of NAL units.
  /// @return True on success, false otherwise.
  using InputBufferCallback = std::function<bool(const std::vector<std::byte>& frameBuffer)>;
  /// @brief Callback type for handling output buffer.
  /// Called when an output frame is ready (for example, after decoding).
  using OutputBufferCallback = std::function<void()>;

  /// @brief Process an incoming H.264 buffer that may contain multiple NAL units.
  /// The input data is expected to contain one or more start codes
  /// (either 0x00000001 or 0x000001) that separate NAL units.
  void ProcessFrame(const rtc::binary& data);

  [[maybe_unused]] void SetOnConfigureCallback(const ConfigureCallback& callback) {
    on_configure_ = std::make_unique<ConfigureCallback >(callback);
  }
  [[maybe_unused]] void SetOnInputBufferCallback(const InputBufferCallback & callback) {
    on_input_buffer_ = std::make_unique<InputBufferCallback >(callback);
  }
  [[maybe_unused]] void SetOnOutputBufferCallback(const OutputBufferCallback & callback) {
    on_output_buffer_ = std::make_unique<OutputBufferCallback >(callback);
  }

private:
  bool is_codec_configured_ = false;
  std::vector<std::byte> sps_;
  std::vector<std::byte> pps_;
  std::int32_t current_width_ = 0;
  std::int32_t current_height_ = 0;

  std::unique_ptr<ConfigureCallback> on_configure_{};
  std::unique_ptr<InputBufferCallback> on_input_buffer_{};
  std::unique_ptr<OutputBufferCallback> on_output_buffer_{};

  /// @brief Removes H.264 emulation prevention bytes from a given byte array.
  /// @param data Pointer to the input data.
  /// @param size Number of bytes in the input.
  /// @return A vector of bytes with the emulation prevention bytes removed.
  static std::vector<uint8_t> RemoveEmulationPreventionBytes(const uint8_t* data, std::size_t size) {
    std::vector<uint8_t> rbsp;
    for (std::size_t i = 0; i < size; i++) {
      // Look ahead for the sequence 0x00 0x00 0x03.
      if (i + 2 < size && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x03) {
        rbsp.push_back(0x00);
        rbsp.push_back(0x00);
        i += 2; // Skip the 0x03.
      } else {
        rbsp.push_back(data[i]);
      }
    }
    return rbsp;
  }

  /// @brief Parses an SPS (Sequence Parameter Set) to extract the frame width and height.
  /// @param data Pointer to the SPS data (after start code).
  /// @param size Size of the SPS data.
  /// @param width Output parameter for the width in pixels.
  /// @param height Output parameter for the height in pixels.
  static void ParseSps(const std::byte* data, std::size_t size, std::int32_t* width, std::int32_t* height) {
    // Remove emulation prevention bytes.
    const auto* byte_data = reinterpret_cast<const uint8_t*>(data);
    std::vector<uint8_t> rbsp = RemoveEmulationPreventionBytes(byte_data, size);
    BitReader br(rbsp.data(), rbsp.size());

    // Parse the SPS according to H.264 Annex A.
    uint32_t profile_idc = br.ReadBits(8);
    br.ReadBits(8); // constraint_set flags and reserved_zero bits.
    uint32_t level_idc = br.ReadBits(8);
    uint32_t seq_parameter_set_id = br.ReadUE();

    // Default chroma_format_idc for baseline.
    uint32_t chroma_format_idc = 1;
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
        profile_idc == 44  || profile_idc == 83  || profile_idc == 86  || profile_idc == 118 ||
        profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
      chroma_format_idc = br.ReadUE();
      if (chroma_format_idc == 3) {
        br.ReadBits(1); // separate_colour_plane_flag.
      }
      br.ReadUE(); // bit_depth_luma_minus8.
      br.ReadUE(); // bit_depth_chroma_minus8.
      br.ReadBits(1); // qpprime_y_zero_transform_bypass_flag.
      uint32_t seq_scaling_matrix_present_flag = br.ReadBits(1);
      if (seq_scaling_matrix_present_flag != 0U) {
        int scaling_list_count = (chroma_format_idc != 3) ? 8 : 12;
        for (int i = 0; i < scaling_list_count; i++) {
          uint32_t seq_scaling_list_present_flag = br.ReadBits(1);
          if (seq_scaling_list_present_flag != 0U) {
            // skip the scaling list details.
          }
        }
      }
    }

    br.ReadUE(); // log2_max_frame_num_minus4.
    auto pic_order_cnt_type = br.ReadUE();
    if (pic_order_cnt_type == 0) {
      br.ReadUE(); // log2_max_pic_order_cnt_lsb_minus4.
    } else if (pic_order_cnt_type == 1) {
      br.ReadBits(1); // delta_pic_order_always_zero_flag.
      br.ReadSE();    // offset_for_non_ref_pic.
      br.ReadSE();    // offset_for_top_to_bottom_field.
      auto num_ref_frames_in_pic_order_cnt_cycle = br.ReadUE();
      for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
        br.ReadSE();  // offset_for_ref_frame[i].
      }
    }
    br.ReadUE(); // max_num_ref_frames.
    br.ReadBits(1); // gaps_in_frame_num_value_allowed_flag.
    auto pic_width_in_mbs_minus1 = br.ReadUE();
    auto pic_height_in_map_units_minus1 = br.ReadUE();
    auto frame_mbs_only_flag = br.ReadBits(1);
    if (frame_mbs_only_flag == 0) {
      br.ReadBits(1); // mb_adaptive_frame_field_flag.
    }
    br.ReadBits(1); // direct_8x8_inference_flag.

    auto frame_cropping_flag = br.ReadBits(1);
    uint32_t frame_crop_left_offset = 0;
    uint32_t frame_crop_right_offset = 0;
    uint32_t frame_crop_top_offset = 0;
    uint32_t frame_crop_bottom_offset = 0;
    if (frame_cropping_flag != 0U) {
      frame_crop_left_offset   = br.ReadUE();
      frame_crop_right_offset  = br.ReadUE();
      frame_crop_top_offset    = br.ReadUE();
      frame_crop_bottom_offset = br.ReadUE();
    }

    // Calculate picture dimensions.
    uint32_t macroblock_width = pic_width_in_mbs_minus1 + 1;
    uint32_t macroblock_height = pic_height_in_map_units_minus1 + 1;
    uint32_t frame_height = macroblock_height * 16;
    if (frame_mbs_only_flag == 0) {
      frame_height *= 2;
    }
    uint32_t width_pixels  = macroblock_width * 16;
    uint32_t height_pixels = frame_height;

    // Determine crop unit sizes based on chroma_format_idc.
    uint32_t crop_unit_x = 0;
    uint32_t crop_unit_y = 0;
    if (chroma_format_idc == 0) {
      crop_unit_x = 1;
      crop_unit_y = 2 - frame_mbs_only_flag;
    } else if (chroma_format_idc == 1) {
      crop_unit_x = 2;
      crop_unit_y = 2 * (2 - frame_mbs_only_flag);
    } else if (chroma_format_idc == 2) {
      crop_unit_x = 2;
      crop_unit_y = 2 - frame_mbs_only_flag;
    } else { // chroma_format_idc == 3.
      crop_unit_x = 1;
      crop_unit_y = 2 - frame_mbs_only_flag;
    }

    if (frame_cropping_flag != 0U) {
      width_pixels  -= (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x;
      height_pixels -= (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y;
    }

    *width  = static_cast<std::int32_t>(width_pixels);
    *height = static_cast<std::int32_t>(height_pixels);
  }
};

inline void H264Parser::ProcessFrame(const rtc::binary& data) {
  // This combined buffer will hold all non-SPS/PPS NALUs.
  std::vector<std::byte> combined_frame_buffer;
  std::uint32_t pos = 0;
  while (pos < data.size()) {
    // Detect a start code (either 00 00 00 01 or 00 00 01).
    std::uint32_t start_code_len = 0;
    if (pos + 4 <= data.size() &&
        std::memcmp(std::next(data.data(), pos), "\x00\x00\x00\x01", 4) == 0) {
      start_code_len = 4;
    } else if (pos + 3 <= data.size() &&
               std::memcmp(std::next(data.data(), pos), "\x00\x00\x01", 3) == 0) {
      start_code_len = 3;
    } else {
      // If no valid start code is found at this position, advance one byte.
      pos++;
      continue;
    }

    // The NALU starts immediately after the start code.
    auto nalu_start = pos + start_code_len;
    // Find the next start code.
    auto next_start = nalu_start;
    while (next_start < data.size()) {
      if (next_start + 3 <= data.size() &&
          std::memcmp(std::next(data.data(), next_start), "\x00\x00\x01", 3) == 0) {
        break;
      }
      if (next_start + 4 <= data.size() &&
          std::memcmp(std::next(data.data(), next_start), "\x00\x00\x00\x01", 4) == 0) {
        break;
      }
      ++next_start;
    }

    // Extract this NALU (without its start code).
    rtc::binary nalu;
    nalu.insert(
        nalu.end(),
        std::next(data.begin(), nalu_start),
        std::next(data.begin() + next_start));

    // Determine the NAL unit type.
    constexpr auto nal_type_mask = std::byte{0x1F};
    // If the NALU is empty, skip it.
    if (nalu.empty()) {
      pos = next_start;
      continue;
    }
    const std::byte nal_header = nalu[0];
    const std::byte nal_unit_type = nal_header & nal_type_mask;
    constexpr auto nal_type_sps = std::byte{0x07};
    constexpr auto nal_type_pps = std::byte{0x08};

    if (nal_unit_type == nal_type_sps) {
      // Process SPS: update stored SPS and extract resolution.
      std::vector<std::byte> new_sps = nalu;
      std::int32_t new_width = 0;
      std::int32_t new_height = 0;
      ParseSps(new_sps.data(), new_sps.size(), &new_width, &new_height);
      if (is_codec_configured_ && (new_width != current_width_ || new_height != current_height_)) {
        is_codec_configured_ = false;
      }
      sps_ = new_sps;
      current_width_ = new_width;
      current_height_ = new_height;
    } else if (nal_unit_type == nal_type_pps) {
      // Process PPS.
      pps_ = nalu;
    } else {
      // For any other NALU (slice, SEI, etc.), add it to the combined frame.
      // Prepend a 4-byte big-endian header with the NALU size.
      auto nalu_size = static_cast<uint32_t>(nalu.size());
      constexpr auto char_bit = 8U;
      constexpr auto full_bitmask = 0xFFU;
      auto header = std::array<std::byte, 4> {
          std::byte{ static_cast<unsigned char>((nalu_size >> (char_bit * 3)) & full_bitmask) },
          std::byte{ static_cast<unsigned char>((nalu_size >> (char_bit * 2)) & full_bitmask) },
          std::byte{ static_cast<unsigned char>((nalu_size >> (char_bit * 1)) & full_bitmask) },
          std::byte{ static_cast<unsigned char>((nalu_size >> (char_bit * 0)) & full_bitmask) }
      };
      combined_frame_buffer.insert(combined_frame_buffer.end(), std::begin(header), std::end(header));
      combined_frame_buffer.insert(combined_frame_buffer.end(), nalu.begin(), nalu.end());
    }
    pos = next_start;
  }

  // After processing all NALUs in this input buffer, trigger configuration
  // if both SPS and PPS have been received and configuration has not yet been done.
  if (!sps_.empty() && !pps_.empty() && !is_codec_configured_) {
    if (on_configure_ != nullptr) {
      (*on_configure_)(sps_, pps_);
    }
    is_codec_configured_ = true;
  }

  // If the codec is configured, and we have any non-SPS/PPS data, forward it.
  if (is_codec_configured_ && !combined_frame_buffer.empty()) {
    if (on_input_buffer_ != nullptr) {
      auto success = (*on_input_buffer_)(combined_frame_buffer);
      (void)success;
    }
    if (on_output_buffer_ != nullptr) {
      (*on_output_buffer_)();
    }
  }
}
