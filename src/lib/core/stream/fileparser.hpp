#pragma once

#undef max

#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "core/rtc/stream.hpp"
#include "utils/units.hpp"

class FileParser : public Stream::Source {
  std::string directory;
  std::string extension;
  std::uint64_t sampleDuration_us;
  std::uint64_t sampleTime_us = 0;
  std::uint32_t counter = -1;
  bool loop;
  std::uint64_t loopTimestampOffset = 0;

protected:
  rtc::binary sample = {};

public:
  FileParser(std::string directory, std::string extension, uint32_t samplesPerSecond, bool loop) {
    this->directory = directory;
    this->extension = extension;
    this->loop = loop;
    this->sampleDuration_us = Units::Nanoseconds / samplesPerSecond;
  }
  FileParser(const FileParser&) = delete;
  FileParser(FileParser&&) = delete;
  auto operator=(const FileParser&) -> FileParser& = delete;
  auto operator=(FileParser&&) -> FileParser& = delete;

  virtual ~FileParser() {
    Stop();
  }

  virtual void Start() override {
    sampleTime_us = std::numeric_limits<std::uint64_t>::max() - sampleDuration_us + 1;
    LoadNextSample();
  }

  virtual void Stop() override {
    sample = {};
    sampleTime_us = 0;
    counter = -1;
  }

  virtual void LoadNextSample() override {
    std::string frame_id = std::to_string(++counter);

    std::string url = directory + "/sample-" + frame_id + extension;
    std::ifstream source(url, std::ios_base::binary);
    if (!source) {
      if (loop && counter > 0) {
        loopTimestampOffset = sampleTime_us;
        counter = -1;
        LoadNextSample();
        return;
      }
      sample = {};
      return;
    }

    std::vector<char> contents((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    auto* b = reinterpret_cast<const std::byte*>(contents.data());
    sample.assign(b, b + contents.size());
    sampleTime_us += sampleDuration_us;
  }

  virtual rtc::binary GetSample() override {
    return sample;
  }

  virtual std::uint64_t GetSampleTime_us() override {
    return sampleTime_us;
  }

};

class OPUSFileParser : public FileParser {
  static const std::uint32_t defaultSamplesPerSecond = 50;

public:
  OPUSFileParser(std::string directory, bool loop, uint32_t samplesPerSecond = OPUSFileParser::defaultSamplesPerSecond) : FileParser(directory, ".opus", samplesPerSecond, loop) {}
};
