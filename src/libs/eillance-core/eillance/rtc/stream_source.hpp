#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "eillance/capture/capture.hpp"
#include "eillance/capture/config.hpp"
#include "eillance/rtc/stream.hpp"

namespace eillance {

class StreamSource final : public Stream::Source {
  Capture capture_;

  Packet sample_{};
  std::vector<std::byte> encodedData_{};

 public:
  explicit StreamSource(Capture capture)
      : capture_(std::move(capture)) {};
  StreamSource(StreamSource&&) = delete;
  StreamSource(StreamSource const&) = delete;
  auto operator=(StreamSource&&) -> StreamSource& = delete;
  auto operator=(StreamSource const&) -> StreamSource& = delete;
  ~StreamSource() = default;

  void Configure(Config config) override { capture_.Configure(config); }

  void Start() override { capture_.Engage(); }

  void LoadNextSample() override { sample_ = capture_.FetchBuffers(); }

  auto GetSample() -> std::vector<std::byte> override { return sample_.buffer(); }

  void Stop() override { capture_.Revoke(); }

  auto GetSampleTime() -> std::uint64_t override { return sample_.timestamp(); }

  auto GetSampleDuration() -> std::uint64_t override { return capture_.GetSampleDuration(); }
};

}  // namespace eillance
