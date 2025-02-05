#include <jni.h>

#include <string>
#include <cassert>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <media/NdkMediaCodec.h>

#include <rtc/rtc.hpp>

#include "hela/eillance/rtc/consumer.hpp"
#include "hela/eillance/logging/logging.h"
#include "hela/eillance/utils/string.hpp"

namespace {

/// @brief Global JVM object
JavaVM* javaVm = nullptr;

}  // namespace

ANativeWindow* nativeWindow = nullptr;
AMediaCodec* mediaCodec = nullptr;
std::vector<std::byte> sps, pps;
bool codecConfigured = false;
// Track the currently configured resolution.
std::int32_t currentWidth = 0, currentHeight = 0;
hela::eillance::RtcConsumer consumer{};

// A simple bitstream reader for SPS parsing.
class BitReader {
public:
  BitReader(const uint8_t* data, std::size_t size)
      : data_(data), size_(size), byte_offset_(0), bit_offset_(0) {}

  // Read a single bit (returns in 'bit').
  bool readBit(uint32_t& bit) {
    if (byte_offset_ >= size_) return false;
    bit = (data_[byte_offset_] >> (7 - bit_offset_)) & 0x01;
    bit_offset_++;
    if (bit_offset_ == 8) {
      bit_offset_ = 0;
      byte_offset_++;
    }
    return true;
  }

  // Read n bits and return as an unsigned integer.
  uint32_t readBits(int n) {
    uint32_t bits = 0;
    for (int i = 0; i < n; i++) {
      uint32_t bit;
      if (!readBit(bit)) break;
      bits = (bits << 1) | bit;
    }
    return bits;
  }

  // Read an unsigned Exp-Golomb coded value.
  uint32_t readUE() {
    int zeros = 0;
    uint32_t bit;
    while (true) {
      if (!readBit(bit)) break;
      if (bit == 0) zeros++;
      else break;
    }
    uint32_t value = 0;
    if (zeros > 0) {
      uint32_t rest = readBits(zeros);
      value = (1u << zeros) - 1 + rest;
    }
    return value;
  }

  // Read a signed Exp-Golomb coded value.
  int32_t readSE() {
    uint32_t codeNum = readUE();
    int32_t value = (codeNum & 1) ? static_cast<int32_t>((codeNum + 1) >> 1)
                                  : -static_cast<int32_t>(codeNum >> 1);
    return value;
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t byte_offset_;
  int bit_offset_;
};

// Helper: Remove emulation prevention bytes (0x03 following 0x00 0x00)
static std::vector<uint8_t> removeEmulationPreventionBytes(const uint8_t* data, std::size_t size) {
  std::vector<uint8_t> rbsp;
  for (std::size_t i = 0; i < size; i++) {
    // Check if the next three bytes match 0x00 0x00 0x03
    if (i + 2 < size && data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x03) {
      rbsp.push_back(0x00);
      rbsp.push_back(0x00);
      i += 2; // Skip the emulation prevention byte (0x03)
    } else {
      rbsp.push_back(data[i]);
    }
  }
  return rbsp;
}

/// @brief A parser for SPS that extracts width and height.
void ParseSps(const std::byte* data, std::size_t size, std::int32_t* width, std::int32_t* height) {
  // Convert to uint8_t pointer and remove emulation prevention bytes.
  const uint8_t* byteData = reinterpret_cast<const uint8_t*>(data);
  std::vector<uint8_t> rbsp = removeEmulationPreventionBytes(byteData, size);
  BitReader br(rbsp.data(), rbsp.size());

  // Parse the SPS as specified in H.264 Annex A.
  uint32_t profile_idc = br.readBits(8);
  br.readBits(8); // constraint_set flags and reserved_zero bits
  uint32_t level_idc = br.readBits(8);
  uint32_t seq_parameter_set_id = br.readUE();

  // Default chroma_format_idc for baseline (if not present).
  uint32_t chroma_format_idc = 1;
  if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
      profile_idc == 44  || profile_idc == 83  || profile_idc == 86  || profile_idc == 118 ||
      profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134) {
    chroma_format_idc = br.readUE();
    if (chroma_format_idc == 3) {
      br.readBits(1); // separate_colour_plane_flag
    }
    br.readUE(); // bit_depth_luma_minus8
    br.readUE(); // bit_depth_chroma_minus8
    br.readBits(1); // qpprime_y_zero_transform_bypass_flag
    uint32_t seq_scaling_matrix_present_flag = br.readBits(1);
    if (seq_scaling_matrix_present_flag) {
      int scalingListCount = (chroma_format_idc != 3) ? 8 : 12;
      for (int i = 0; i < scalingListCount; i++) {
        uint32_t seq_scaling_list_present_flag = br.readBits(1);
        if (seq_scaling_list_present_flag) {
          // For simplicity, we skip the scaling list details.
          // A complete implementation would parse these scaling lists.
        }
      }
    }
  }

  br.readUE(); // log2_max_frame_num_minus4
  uint32_t pic_order_cnt_type = br.readUE();
  if (pic_order_cnt_type == 0) {
    br.readUE(); // log2_max_pic_order_cnt_lsb_minus4
  } else if (pic_order_cnt_type == 1) {
    br.readBits(1); // delta_pic_order_always_zero_flag
    br.readSE(); // offset_for_non_ref_pic
    br.readSE(); // offset_for_top_to_bottom_field
    uint32_t num_ref_frames_in_pic_order_cnt_cycle = br.readUE();
    for (uint32_t i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
      br.readSE(); // offset_for_ref_frame[i]
    }
  }
  br.readUE(); // max_num_ref_frames
  br.readBits(1); // gaps_in_frame_num_value_allowed_flag
  uint32_t pic_width_in_mbs_minus1 = br.readUE();
  uint32_t pic_height_in_map_units_minus1 = br.readUE();
  uint32_t frame_mbs_only_flag = br.readBits(1);
  if (frame_mbs_only_flag == 0) {
    br.readBits(1); // mb_adaptive_frame_field_flag
  }
  br.readBits(1); // direct_8x8_inference_flag

  uint32_t frame_cropping_flag = br.readBits(1);
  uint32_t frame_crop_left_offset = 0;
  uint32_t frame_crop_right_offset = 0;
  uint32_t frame_crop_top_offset = 0;
  uint32_t frame_crop_bottom_offset = 0;
  if (frame_cropping_flag) {
    frame_crop_left_offset = br.readUE();
    frame_crop_right_offset = br.readUE();
    frame_crop_top_offset = br.readUE();
    frame_crop_bottom_offset = br.readUE();
  }

  // Calculate the width and height in macroblocks.
  uint32_t macroblock_width = pic_width_in_mbs_minus1 + 1;
  uint32_t macroblock_height = pic_height_in_map_units_minus1 + 1;
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
  } else { // chroma_format_idc == 3
    crop_unit_x = 1;
    crop_unit_y = 2 - frame_mbs_only_flag;
  }

  if (frame_cropping_flag) {
    width_pixels  -= (frame_crop_left_offset + frame_crop_right_offset) * crop_unit_x;
    height_pixels -= (frame_crop_top_offset + frame_crop_bottom_offset) * crop_unit_y;
  }

  *width = static_cast<std::int32_t>(width_pixels);
  *height = static_cast<std::int32_t>(height_pixels);
}

/// @brief Configures the MediaCodec decoder using the given SPS and PPS.
/// Also saves the current resolution.
void ConfigureCodec(const std::byte* spsData, std::size_t spsSize,
                    const std::byte* ppsData, std::size_t ppsSize) {
  if (mediaCodec != nullptr) {
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  mediaCodec = AMediaCodec_createDecoderByType("video/avc");
  AMediaFormat* format = AMediaFormat_new();
  AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");

  std::int32_t width = 0;
  std::int32_t height = 0;
  ParseSps(spsData, spsSize, &width, &height);
  currentWidth = width;
  currentHeight = height;
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, width);
  AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, height);
  AMediaFormat_setBuffer(format, "csd-0", spsData, spsSize);
  AMediaFormat_setBuffer(format, "csd-1", ppsData, ppsSize);

  AMediaCodec_configure(mediaCodec, format, nativeWindow, nullptr, 0);
  AMediaCodec_start(mediaCodec);
  AMediaFormat_delete(format);
  codecConfigured = true;
}

/// @brief Processes an incoming frame by parsing the NAL unit header and handling SPS/PPS
/// changes (including resolution changes) before decoding the frame.
void ProcessFrame(const rtc::binary& data) {
  // Identify the start code (either 00 00 00 01 or 00 00 01)
  std::size_t start_code_len = 0;
  if (data.size() >= 4 && std::memcmp(data.data(), "\x00\x00\x00\x01", 4) == 0) {
    start_code_len = 4;
  } else if (data.size() >= 3 && std::memcmp(data.data(), "\x00\x00\x01", 3) == 0) {
    start_code_len = 3;
  } else {
    return;
  }

  // Extract the NAL unit header byte and get its lower 5 bits.
  constexpr std::byte nal_type_mask = std::byte{0x1F};
  // The first byte after the start code contains:
  // forbidden_zero_bit (1 bit), nal_ref_idc (2 bits) and nal_unit_type (5 bits).
  std::byte nal_header = data[start_code_len];
  std::byte nal_unit_type = nal_header & nal_type_mask;

  constexpr std::byte nal_type_sps = std::byte{0x07};
  constexpr std::byte nal_type_pps = std::byte{0x08};

  // If we received an SPS, store it and check if a reconfiguration is needed.
  if (nal_unit_type == nal_type_sps) { // SPS
    // Store the new SPS data (without the start code)
    std::vector<std::byte> newSPS(data.begin() + start_code_len, data.end());
    // Parse new resolution from the new SPS.
    std::int32_t newWidth = 0;
    std::int32_t newHeight = 0;
    ParseSps(newSPS.data(), newSPS.size(), &newWidth, &newHeight);
    // If the codec is already configured and the resolution has changed,
    // reconfigure the codec.
    if (codecConfigured && (newWidth != currentWidth || newHeight != currentHeight)) {
      // Log reconfiguration event (if you have a logging facility)
      LOGI << "Resolution change detected. Reconfiguring codec: " <<
                    "Old [" << currentWidth << " x " << currentHeight << "] -> New [" << newWidth << " x " << newHeight << "]";
      // Stop and delete the old codec and mark it unconfigured.
      AMediaCodec_stop(mediaCodec);
      AMediaCodec_delete(mediaCodec);
      mediaCodec = nullptr;
      codecConfigured = false;
      // Update the stored SPS.
      sps = newSPS;
    } else {
      sps = newSPS;
    }
  } else if (nal_unit_type == nal_type_pps) { // PPS
    pps.assign(data.begin() + start_code_len, data.end());
  }

  // If both SPS and PPS have been received and the codec is not configured, configure it.
  if (!sps.empty() && !pps.empty() && !codecConfigured) {
    ConfigureCodec(sps.data(), sps.size(), pps.data(), pps.size());
  }

  // If codec is configured, prepare the frame for decoding.
  if (codecConfigured) {
    std::size_t nal_size = data.size() - start_code_len;
    uint8_t header[4] = {
        static_cast<uint8_t>(nal_size >> 24),
        static_cast<uint8_t>(nal_size >> 16),
        static_cast<uint8_t>(nal_size >> 8),
        static_cast<uint8_t>(nal_size)
    };

    int32_t idx = AMediaCodec_dequeueInputBuffer(mediaCodec, 10000);
    if (idx >= 0) {
      std::size_t buf_size = 0;
      uint8_t* buf = AMediaCodec_getInputBuffer(mediaCodec, idx, &buf_size);
      if (buf != nullptr && (nal_size + 4) <= buf_size) {
        std::memcpy(buf, header, 4);
        std::memcpy(buf + 4, data.data() + start_code_len, nal_size);
        AMediaCodec_queueInputBuffer(mediaCodec, idx, 0, nal_size + 4, 0, 0);
      }
    }

    AMediaCodecBufferInfo info;
    int32_t out_idx = AMediaCodec_dequeueOutputBuffer(mediaCodec, &info, 0);
    if (out_idx >= 0) {
      AMediaCodec_releaseOutputBuffer(mediaCodec, out_idx, true);
    }
  }
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_initDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv* env, jobject, jobject surface) {
  nativeWindow = ANativeWindow_fromSurface(env, surface);

  consumer.SetName("CLI::" + std::string(StringUtils::RandomString()));
  consumer.Start();
  consumer.SetClientAddedCallback([](){
    consumer.SendStreamRequest();
  });
  consumer.SetFrameReceivedCallback([](const rtc::binary& frame) {
    ProcessFrame(frame);
  });
}

extern "C" JNIEXPORT void JNICALL
Java_hela_eillance_ObserverActivity_releaseDecoder(  // NOLINT(*-identifier-naming)
    JNIEnv*, jobject) {
  consumer.Stop();
  if (mediaCodec != nullptr) {
    AMediaCodec_stop(mediaCodec);
    AMediaCodec_delete(mediaCodec);
    mediaCodec = nullptr;
  }
  if (nativeWindow != nullptr) {
    ANativeWindow_release(nativeWindow);
    nativeWindow = nullptr;
  }
}

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  javaVm = vm;
  return JNI_VERSION_1_6;
}
