#pragma once

#include <string>
#include <vector>
#include <fstream>

#include "opus.h"

#include "stream.hpp"
#include "audio/recorder.h"


class AudioBufferSource: public StreamSource {
	static constexpr auto fastPathFramesPerBuf = 24000 / 200;// was 976; // todo real
	static constexpr auto AUDIO_CHANNELS = kMonoChannelCount;
	static constexpr auto MAX_PCM_BUFFER_SIZE = 8192;
	std::array<float, MAX_PCM_BUFFER_SIZE> pcm_buffer = {};
	std::array<unsigned char, MAX_PCM_BUFFER_SIZE> encoded_buffer = {};

protected:
	std::unique_ptr<Recorder> recorder = nullptr;

    OpusEncoder* opus_encoder;

public:
	AudioBufferSource() {
		recorder = std::make_unique<Recorder>();

		// opus
        auto error = 0;
		opus_encoder = opus_encoder_create(24000, AUDIO_CHANNELS, OPUS_APPLICATION_AUDIO, &error);
		assert(error == OPUS_OK);
		assert(opus_encoder != nullptr);
	}

    virtual void start() {
		recorder->start();
	}

    virtual void stop() {
//		recorder->codec()->stop(); // todo
    }

    virtual void loadNextSample() {
	    sampleTime_us += (1. / 30.) * 1000. * 1000.; // todo make it normal
		sample.clear();
		// todo threadsafe
		auto frames = recorder->read(pcm_buffer.data(), fastPathFramesPerBuf);
		if (likely(frames > 0)) {
			auto size = opus_encode_float(opus_encoder, &pcm_buffer[0], frames,
										  encoded_buffer.data(), encoded_buffer.size());
			if (likely(size > 0)) {
				sample.insert(
						sample.end(),
						reinterpret_cast<std::byte *>(&encoded_buffer[0]),
						reinterpret_cast<std::byte *>(&encoded_buffer[size])
				);
			}
		}
    }

};
