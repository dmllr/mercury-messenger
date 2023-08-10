#pragma once

#include <string>
#include <vector>
#include <fstream>

#include "opus.h"

#include "rtc/stream.hpp"
#include "audio/recorder.h"


class AudioBufferSource: public StreamSource {
	static constexpr auto MAX_PCM_BUFFER_SIZE = Recorder::framesPerBurst * Recorder::channelCount * sizeof(float);
	std::array<float, MAX_PCM_BUFFER_SIZE> pcm_buffer = {};
	std::array<unsigned char, MAX_PCM_BUFFER_SIZE> encoded_buffer = {};

	rtc::binary encoded_data;
	std::chrono::time_point<std::chrono::steady_clock> start_time;
	std::chrono::time_point<std::chrono::steady_clock> last_time;

protected:
	std::unique_ptr<Recorder> recorder = nullptr;

    OpusEncoder* opus_encoder;

public:
	AudioBufferSource() {
		recorder = std::make_unique<Recorder>();

		// opus
        auto error = 0;
		opus_encoder = opus_encoder_create(Recorder::sampleRate, Recorder::channelCount, OPUS_APPLICATION_AUDIO, &error);
		assert(error == OPUS_OK);
		assert(opus_encoder != nullptr);
	}

    virtual void start() override {
		recorder->start();

		start_time = std::chrono::steady_clock::now();
	}

    virtual void stop() override {
		recorder->stop();
    }

    uint64_t getSampleTime_us() override {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
    }

    uint64_t getSampleDuration_us() override {
        return 1000000 / 30; // todo make it normal
    }

	void loadNextSample() override {
		// todo threadsafe
		auto frames = recorder->read(pcm_buffer.data(), Recorder::framesPerBurst);
		if (likely(frames > 0)) {
			auto size = opus_encode_float(opus_encoder, &pcm_buffer[0], Recorder::framesPerBurst,
										  encoded_buffer.data(), encoded_buffer.size());
			if (likely(size > 0)) {
				encoded_data.insert(
						encoded_data.end(),
						reinterpret_cast<std::byte *>(&encoded_buffer[0]),
						reinterpret_cast<std::byte *>(&encoded_buffer[size])
				);
			}
		}
	}

    rtc::binary getSample() override {
        auto sample = encoded_data;
        encoded_data.clear();

        return sample;
    }

};
