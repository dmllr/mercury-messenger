#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include <string>
#include <cassert>
#include <concepts>

#include <rtc/rtc.hpp>
#include "eillance/capture/video_config.hpp"
#include "eillance/logging/logging.h"
#include "eillance/utils/bit_reader.hpp"

namespace eillance {

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
  using ConfigureCallback = std::function<void(const VideoConfig& conf)>;

  /// @brief Callback type for handling input buffer.
  /// The frame buffer contains a series of NAL units where each NALU is preceded by a
  /// 4╬ô├ç├ªbyte big╬ô├ç├ªendian size header.
  /// @param frameBuffer The frame buffer containing a series of NAL units.
  /// @return True on success, false otherwise.
  using InputBufferCallback = std::function<void(const std::vector<std::byte>& frameBuffer)>;

  /// @brief Process an incoming H.264 buffer that may contain multiple NAL units.
  /// The input data is expected to contain one or more start codes
  /// (either 0x00000001 or 0x000001) that separate NAL units.
  void ProcessFrame(const rtc::binary& data);

  /// @brief Resets the internal state of the parser preparing for a new stream.
  void Reset() {
    current_config_ = {.width = 0, .height = 0, .rotation = 0};
    on_configure_ = {};
  }

  [[maybe_unused]] void SetOnConfigureCallback(const ConfigureCallback& callback) {
    on_configure_ = std::make_unique<ConfigureCallback>(callback);
  }

private:
  VideoConfig current_config_ = {.width = 0, .height = 0, .rotation = 0};

  std::unique_ptr<ConfigureCallback> on_configure_{};

  static constexpr auto nal_type_mask = std::byte{0x1F};
  static constexpr auto nal_type_sps = std::byte{0x07};
  static constexpr auto nal_type_pps = std::byte{0x08};
  static constexpr auto nal_type_sei = std::byte{0x06};


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
        i += 2;  // Skip the 0x03.
      } else {
        rbsp.push_back(data[i]);
      }
    }
    return rbsp;
  }

  /// @brief Parses an SPS (Sequence Parameter Set) to extract the frame width and height.
  /// @param data Pointer to the SPS data (after start code).
  /// @param size Size of the SPS data.
  /// @return VideoConfig containing the frame width and height.
  static auto ParseSps(const std::vector<std::byte>& sps) -> VideoConfig {
    // Remove emulation prevention bytes.
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    const auto* byte_data = reinterpret_cast<const uint8_t*>(sps.data());
    const std::vector<uint8_t> rbsp = RemoveEmulationPreventionBytes(byte_data, sps.size());
    BitReader br(rbsp.data(), rbsp.size());

    const auto nal_header = br.ReadByte();
    assert(nal_header == 0x67);

    // Parse the SPS according to H.264 Annex A.
    const auto profile_idc = br.ReadByte();
    br.ReadByte();  // constraint_set flags and reserved_zero bits.
    [[maybe_unused]] const auto level_idc = br.ReadByte();
    [[maybe_unused]] const auto seq_parameter_set_id = br.ReadUE();

    // Default chroma_format_idc for baseline.
    uint32_t chroma_format_idc = 1;
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
        profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
        profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
      chroma_format_idc = br.ReadUE();
      if (chroma_format_idc == 3) {
        br.ReadBits(1);  // separate_colour_plane_flag.
      }
      br.ReadUE();     // bit_depth_luma_minus8.
      br.ReadUE();     // bit_depth_chroma_minus8.
      br.ReadBits(1);  // qpprime_y_zero_transform_bypass_flag.
      const auto seq_scaling_matrix_present_flag = br.ReadBits(1);
      if (seq_scaling_matrix_present_flag != 0U) {
        const auto scaling_list_count = (chroma_format_idc != 3) ? 8 : 12;
        for (int i = 0; i < scaling_list_count; i++) {
          const auto seq_scaling_list_present_flag = br.ReadBits(1);
          if (seq_scaling_list_present_flag != 0U) {
            // skip the scaling list details.
          }
        }
      }
    }

    br.ReadUE();  // log2_max_frame_num_minus4.
    auto pic_order_cnt_type = br.ReadUE();
    if (pic_order_cnt_type == 0) {
      br.ReadUE();  // log2_max_pic_order_cnt_lsb_minus4.
    } else if (pic_order_cnt_type == 1) {
      br.ReadBits(1);  // delta_pic_order_always_zero_flag.
      br.ReadSE();     // offset_for_non_ref_pic.
      br.ReadSE();     // offset_for_top_to_bottom_field.
      const auto num_ref_frames_in_pic_order_cnt_cycle = br.ReadUE();
      for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
        br.ReadSE();  // offset_for_ref_frame[i].
      }
    }
    br.ReadUE();     // max_num_ref_frames.
    br.ReadBits(1);  // gaps_in_frame_num_value_allowed_flag.
    const auto pic_width_in_mbs_minus1 = br.ReadUE();
    const auto pic_height_in_map_units_minus1 = br.ReadUE();
    const auto frame_mbs_only_flag = br.ReadBits(1);
    if (frame_mbs_only_flag == 0) {
      br.ReadBits(1);  // mb_adaptive_frame_field_flag.
    }
    br.ReadBits(1);  // direct_8x8_inference_flag.

    const auto frame_cropping_flag = br.ReadBits(1);
    uint32_t frame_crop_left_offset = 0;
    uint32_t frame_crop_right_offset = 0;
    uint32_t frame_crop_top_offset = 0;
    uint32_t frame_crop_bottom_offset = 0;
    if (frame_cropping_flag != 0U) {
      frame_crop_left_offset = br.ReadUE();
      frame_crop_right_offset = br.ReadUE();
      frame_crop_top_offset = br.ReadUE();
      frame_crop_bottom_offset = br.ReadUE();
    }

    auto vui_parameters_present_flag = br.ReadBits(1);
    if (vui_parameters_present_flag != 0U) {
      // VUI parameters parsing
      auto aspect_ratio_info_present_flag = br.ReadBits(1);
      if (aspect_ratio_info_present_flag != 0U) {
        // Skip aspect ratio info
        auto aspect_ratio_idc = br.ReadByte();
        if (aspect_ratio_idc == 255) {  // Extended_SAR
          br.ReadBits(16);  // sar_width
          br.ReadBits(16);  // sar_height
        }
      }

      // Skip other VUI parameters until we get to the SEI message parsing
      // ... (other VUI parameter parsing)
    }

    // Calculate picture dimensions.
    const auto macroblock_width = pic_width_in_mbs_minus1 + 1;
    const auto macroblock_height = pic_height_in_map_units_minus1 + 1;
    uint32_t frame_height = macroblock_height * 16;
    if (frame_mbs_only_flag == 0) {
      frame_height *= 2;
    }
    uint32_t width_pixels = macroblock_width * 16;
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
    } else {  // chroma_format_idc == 3.
      crop_unit_x = 1;
      crop_unit_y = 2 - frame_mbs_only_flag;
    }

    if (frame_cropping_flag != 0U) {
      width_pixels -= (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x;
      height_pixels -= (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y;
    }

    auto conf = VideoConfig();
    conf.width = width_pixels;
    conf.height = height_pixels;
    // Note: rotation will be set from SEI parsing
    return conf;
  }

  /// @brief Parse SEI (Supplemental Enhancement Information) message for rotation metadata
  /// @param sei SEI message data
  /// @return Rotation angle in degrees (0, 90, 180, or 270)
  static uint32_t ParseSeiForRotation(const std::vector<std::byte>& sei) {
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    const auto* byte_data = reinterpret_cast<const uint8_t*>(sei.data());
    const std::vector<uint8_t> rbsp = RemoveEmulationPreventionBytes(byte_data, sei.size());

    // Skip NAL header
    if (rbsp.size() < 2) return 0;

    size_t pos = 1; // Skip the NAL header byte

    // Parse SEI payload type
    uint32_t payload_type = 0;
    while (pos < rbsp.size() && rbsp[pos] == 0xFF) {
      payload_type += 0xFF;
      pos++;
    }
    if (pos < rbsp.size()) {
      payload_type += rbsp[pos++];
    }

    // Parse SEI payload size
    uint32_t payload_size = 0;
    while (pos < rbsp.size() && rbsp[pos] == 0xFF) {
      payload_size += 0xFF;
      pos++;
    }
    if (pos < rbsp.size()) {
      payload_size += rbsp[pos++];
    }

    // Check if we have a user data unregistered SEI message (payload_type == 5)
    if (payload_type == 5 && pos + payload_size <= rbsp.size()) {
      // Log the first few bytes to help with debugging
      std::string payload_hex;
      for (size_t i = 0; i < std::min(payload_size, 32u); i++) {
        char hex[8];
        snprintf(hex, sizeof(hex), "%02x ", rbsp[pos + i]);
        payload_hex += hex;
      }
      // Search for UUID (first 16 bytes)
      if (payload_size >= 16) {
        // Skip UUID (16 bytes)
        pos += 16;

        // Convert remaining payload to string for easier parsing
        std::string payload_str;
        for (size_t i = pos; i < pos + payload_size - 16 && i < rbsp.size(); i++) {
          payload_str += static_cast<char>(rbsp[i]);
        }

        // Look for rotation marker
        size_t rotation_pos = payload_str.find("rotation-degrees:");
        if (rotation_pos != std::string::npos) {
          rotation_pos += 17; // Skip "rotation-degrees:"

          // Parse the rotation value
          if (rotation_pos < payload_str.length()) {
            if (payload_str.substr(rotation_pos, 2) == "90") {
              return 90;
            } else if (payload_str.substr(rotation_pos, 3) == "180") {
              return 180;
            } else if (payload_str.substr(rotation_pos, 3) == "270") {
              return 270;
            }
          }
        }
      }
    }

    return 0; // Default: no rotation
  }
};

void H264Parser::ProcessFrame(const rtc::binary& data) {
  std::uint32_t pos = 0;
  bool config_updated = false;

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
    const auto nalu_start = pos + start_code_len;

    // Skip if we're at the end of the data
    if (nalu_start >= data.size()) {
      break;
    }

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
        std::next(data.begin(), next_start));

    // Determine the NAL unit type.
    // If the NALU is empty, skip it.
    if (nalu.empty()) {
      pos = next_start;
      continue;
    }

    const std::byte nal_header = nalu[0];
    const std::byte nal_unit_type = nal_header & nal_type_mask;

    LOGD << "Found NAL unit type: " << static_cast<int>(nal_unit_type) << " size: " << nalu.size();

    if (nal_unit_type == nal_type_sps) {
      // Process SPS: update stored SPS and extract resolution.
      const std::vector<std::byte>& new_sps = nalu;
      auto config = ParseSps(new_sps);

      // Preserve rotation if we already have it
      config.rotation = current_config_.rotation;

      if (config != current_config_) {
        current_config_.width = config.width;
        current_config_.height = config.height;
        config_updated = true;
        LOGI << "Updated config from SPS: " << config.width << "x" << config.height;
      }
    } else if (nal_unit_type == nal_type_sei) {
      // Process SEI: look for rotation information
      LOGI << "Processing SEI NAL unit";
      const std::vector<std::byte>& sei_data = nalu;
      uint32_t rotation = ParseSeiForRotation(sei_data);

      if (rotation != current_config_.rotation) {
        current_config_.rotation = rotation;
        config_updated = true;
        LOGI << "Updated rotation from SEI: " << rotation << " degrees";
      }
    }

    pos = next_start;
  }

  // If either SPS or SEI updates changed our configuration, notify via callback
  if (config_updated && on_configure_ != nullptr) {
    LOGI << "Configuration changed, notifying: " << current_config_.width << "x"
         << current_config_.height << " rotation: " << current_config_.rotation;
    (*on_configure_)(current_config_);
  }
}

}  // namespace eillance
