#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "eillance/capture/video_config.hpp"
#include "eillance/logging/logging.h"

namespace eillance {

// A self-contained H.264 NAL unit parser, focused on finding and parsing the
// Sequence Parameter Set (SPS) to extract video dimensions.
class H264Parser {
public:
  void SetOnConfigureCallback(std::function<void(VideoConfig)> callback) {
    on_configure_callback_ = std::move(callback);
  }

  void Reset() {
    is_configured_ = false;
  }

  // Processes a raw H.264 frame, which may contain one or more NAL units.
  void ProcessFrame(const std::vector<std::byte>& frame) {
    if (is_configured_ || !on_configure_callback_) {
      return;
    }

    size_t offset = 0;
    while (offset < frame.size()) {
      auto start_code_pos = FindNalStart(frame, offset);
      if (start_code_pos == std::string::npos) {
        break;  // No more NAL units found in the frame.
      }

      // Find the end of the current NAL unit.
      auto next_start_code_pos = FindNalStart(frame, start_code_pos + 3);
      auto nal_unit_end = (next_start_code_pos == std::string::npos) ? frame.size() : next_start_code_pos;

      // Determine start code length (3 or 4 bytes).
      const size_t start_code_len = (frame[start_code_pos + 2] == std::byte{0}) ? 4 : 3;
      const size_t nal_header_pos = start_code_pos + start_code_len;

      if (nal_header_pos < nal_unit_end) {
        // The NAL unit type is in the lower 5 bits of the header byte.
        uint8_t nal_type = static_cast<uint8_t>(frame[nal_header_pos]) & 0x1F;

        if (nal_type == 7) {  // NAL unit type 7 is SPS.
          // The actual SPS data for parsing excludes the NAL header byte.
          std::vector<uint8_t> sps_payload;
          sps_payload.reserve(nal_unit_end - (nal_header_pos + 1));
          for (size_t i = nal_header_pos + 1; i < nal_unit_end; ++i) {
            sps_payload.push_back(static_cast<uint8_t>(frame[i]));
          }

          ParseSPS(sps_payload);
          // Once configured, we can stop parsing subsequent frames.
          if (is_configured_) return;
        }
      }
      offset = nal_unit_end;
    }
  }

private:
  // Finds the position of the next NAL unit start code (0x000001).
  static size_t FindNalStart(const std::vector<std::byte>& data, size_t offset) {
    for (size_t i = offset; i + 2 < data.size(); ++i) {
      if (data[i] == std::byte{0} && data[i + 1] == std::byte{0} && data[i + 2] == std::byte{1}) {
        // Check for 4-byte start code (0x00000001) vs 3-byte (0x000001)
        return (i > 0 && data[i - 1] == std::byte{0}) ? i - 1 : i;
      }
    }
    return std::string::npos;
  }

  // A simple bitstream reader to handle exponential-Golomb decoding.
  struct BitReader {
    const std::vector<uint8_t>& data;
    size_t bit_pos = 0;

    uint32_t read_bits(int n) {
      uint32_t val = 0;
      for (int i = 0; i < n; ++i) {
        size_t current_bit = bit_pos + i;
        size_t byte_idx = current_bit / 8;
        size_t bit_idx = 7 - (current_bit % 8);
        if (byte_idx < data.size()) {
          val = (val << 1) | ((data[byte_idx] >> bit_idx) & 1);
        }
      }
      bit_pos += n;
      return val;
    }

    uint32_t read_ue() {  // Unsigned Exponential-Golomb
      int leading_zeros = 0;
      while (bit_pos < data.size() * 8 && read_bits(1) == 0) {
        leading_zeros++;
      }
      if (leading_zeros > 31) return 0;
      return (1 << leading_zeros) - 1 + read_bits(leading_zeros);
    }
  };

  // This is a simplified SPS parser that extracts just enough information
  // to determine video resolution. It is not a complete implementation.
  void ParseSPS(const std::vector<uint8_t>& sps) {
    if (sps.empty()) return;

    BitReader reader{sps};

    uint8_t profile_idc = reader.read_bits(8);
    reader.read_bits(16);  // Skip constraint flags and level_idc
    reader.read_ue();      // Skip seq_parameter_set_id

    uint32_t chroma_format_idc = 1;  // Default for many profiles.
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122
      || profile_idc == 244 || profile_idc == 44 || profile_idc == 83
      || profile_idc == 86 || profile_idc == 118 || profile_idc == 128
      || profile_idc == 138) {
      chroma_format_idc = reader.read_ue();
      if (chroma_format_idc == 3) reader.read_bits(1);  // separate_colour_plane_flag
      reader.read_ue();                                 // bit_depth_luma_minus8
      reader.read_ue();                                 // bit_depth_chroma_minus8
      reader.read_bits(1);                              // qpprime_y_zero_transform_bypass_flag
      if (reader.read_bits(1)) {                        // seq_scaling_matrix_present_flag
        // This is a complex part of the spec; skipping it is usually fine for getting resolution.
        int limit = (chroma_format_idc != 3) ? 8 : 12;
        for (int i = 0; i < limit; i++) {
          if (reader.read_bits(1)) {  // seq_scaling_list_present_flag[i]
            int size = (i < 6) ? 16 : 64;
            for (int j = 0; j < size; j++) reader.read_ue();  // Scaling list delta
          }
        }
      }
    }

    reader.read_ue();  // log2_max_frame_num_minus4
    auto pic_order_cnt_type = reader.read_ue();
    if (pic_order_cnt_type == 0) {
      reader.read_ue();  // log2_max_pic_order_cnt_lsb_minus4
    } else if (pic_order_cnt_type == 1) {
      reader.read_bits(1);  // delta_pic_order_always_zero_flag
      reader.read_ue();     // offset_for_non_ref_pic
      reader.read_ue();     // offset_for_top_to_bottom_field
      auto num_ref_frames = reader.read_ue();
      for (uint32_t i = 0; i < num_ref_frames; i++) reader.read_ue();
    }
    reader.read_ue();     // max_num_ref_frames
    reader.read_bits(1);  // gaps_in_frame_num_value_allowed_flag

    auto pic_width_in_mbs_minus1 = reader.read_ue();
    auto pic_height_in_map_units_minus1 = reader.read_ue();
    auto frame_mbs_only_flag = reader.read_bits(1);

    if (frame_mbs_only_flag == 0) {
      reader.read_bits(1);  // mb_adaptive_frame_field_flag
    }
    reader.read_bits(1);  // direct_8x8_inference_flag

    uint32_t width = (pic_width_in_mbs_minus1 + 1) * 16;
    uint32_t height = (2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16;

    if (reader.read_bits(1)) {  // frame_cropping_flag
      auto crop_left = reader.read_ue();
      auto crop_right = reader.read_ue();
      auto crop_top = reader.read_ue();
      auto crop_bottom = reader.read_ue();

      uint32_t crop_unit_x = 1;
      uint32_t crop_unit_y = (2 - frame_mbs_only_flag);
      if (chroma_format_idc == 1) {  // 4:2:0
        crop_unit_x = 2;
        crop_unit_y *= 2;
      } else if (chroma_format_idc == 2) {  // 4:2:2
        crop_unit_x = 2;
      }

      width -= (crop_left + crop_right) * crop_unit_x;
      height -= (crop_top + crop_bottom) * crop_unit_y;
    }

    if (width > 0 && height > 0) {
      LOGI << "H264Parser: Decoded SPS. Resolution: " << width << "x" << height;
      VideoConfig conf;
      conf.width = width;
      conf.height = height;
      on_configure_callback_(conf);
      is_configured_ = true;
    } else {
      LOGE << "H264Parser: Failed to decode a valid resolution from SPS.";
    }
  }

  std::function<void(VideoConfig)> on_configure_callback_;
  bool is_configured_ = false;
};

}  // namespace eillance
