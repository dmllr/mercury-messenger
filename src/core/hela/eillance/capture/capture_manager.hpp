#pragma once

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iostream>
#include <memory>
#include <atomic>

#include "hela/eillance/capture/audio_capture.hpp"
#include "hela/eillance/capture/video_capture.hpp"
#include "hela/eillance/common/packet.hpp"
#include "hela/eillance/common/stream_source_type.hpp"

namespace hela::eillance {

class CaptureManager {
public:
  enum class ConsumerID : std::uint8_t {
    rtcAudio,
    rtcVideo,
  };

private:
  struct ConsumerData {
    StreamSourceType type;
    std::vector<Packet>::iterator position;
  };

  AudioCapture audio_capture_{};
  VideoCapture video_capture_{};

  std::vector<Packet> audio_packets_{};
  std::vector<Packet> video_packets_{};

  std::unordered_map<ConsumerID, ConsumerData> consumers_;
  std::atomic_bool engaged_{false};

public:
  CaptureManager() {
    audio_capture_.SetDataCallback([this](Packet packet) {
      audio_packets_.emplace_back(std::move(packet));
    });

    video_capture_.SetDataCallback([this](Packet packet) {
      video_packets_.emplace_back(std::move(packet));
    });
  }

  void RegisterConsumer(ConsumerID id, StreamSourceType type) {
    if (type == StreamSourceType::audio) {
      consumers_[id] = { type, audio_packets_.begin() };
    }
    if (type == StreamSourceType::video) {
      consumers_[id] = { type, video_packets_.begin() };
    }
    if (!consumers_.empty() && !engaged_) {
      Engage();
    }
  }

  void DeregisterConsumer(ConsumerID id) {
    consumers_.erase(id);
    if (consumers_.empty()) {
      Revoke();
    }
  }

  auto GetSample(ConsumerID id) -> rtc::binary {
    auto& consumer_data = consumers_[id];
    auto& position = consumer_data.position;
    auto& type = consumer_data.type;

    // Move Packet data based on the consumer's type
    if (type == StreamSourceType::audio) {
      auto sample = std::move(*position);
      ++position;
      return sample.data;
    }
    if (type == StreamSourceType::video) {
      auto sample = std::move(*position);
      ++position;
      return sample.data;
    }
    // Handle invalid consumer type
    throw std::runtime_error("Invalid consumer type");
  }

private:
  void Engage() {
    video_capture_.Engage();
    audio_capture_.Engage();
    engaged_ = true;
  }

  void Revoke() {
    audio_capture_.Revoke();
    video_capture_.Revoke();
    engaged_ = false;
  }

};

} // namespace hela::eillance
