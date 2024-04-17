#pragma once

#undef max

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <limits>

#include "rtc/stream.hpp"

class FileParser: public StreamSource {
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
        this->sampleDuration_us = 1000 * 1000 / samplesPerSecond;
    }

    FileParser() {}

    virtual ~FileParser() {
        stop();
    }

    virtual void start() override {
        sampleTime_us = std::numeric_limits<std::uint64_t>::max() - sampleDuration_us + 1;
        loadNextSample();
    }

    virtual void stop() override {
        sample = {};
        sampleTime_us = 0;
        counter = -1;
    }

    virtual void loadNextSample() override {
        std::string frame_id = std::to_string(++counter);

        std::string url = directory + "/sample-" + frame_id + extension;
        std::ifstream source(url, std::ios_base::binary);
        if (!source) {
            if (loop && counter > 0) {
                loopTimestampOffset = sampleTime_us;
                counter = -1;
                loadNextSample();
                return;
            }
            sample = {};
            return;
        }

        std::vector<char> contents((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
        auto *b = reinterpret_cast<const std::byte*>(contents.data());
        sample.assign(b, b + contents.size());
        sampleTime_us += sampleDuration_us;
    }

    virtual rtc::binary getSample() override {
        return sample;
    }

    virtual std::uint64_t getSampleTime_us() override {
        return sampleTime_us;
    }

    virtual std::uint64_t getSampleDuration_us() override {
        return sampleDuration_us;
    }
};

class OPUSFileParser: public FileParser {
    static const std::uint32_t defaultSamplesPerSecond = 50;

public:
    OPUSFileParser(std::string directory, bool loop, uint32_t samplesPerSecond = OPUSFileParser::defaultSamplesPerSecond) { }
};
