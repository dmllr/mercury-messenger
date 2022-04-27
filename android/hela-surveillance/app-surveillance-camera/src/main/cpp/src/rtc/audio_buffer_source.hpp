#pragma once

#include <string>
#include <vector>
#include <fstream>
#include "stream.hpp"
#include "audio/audio_recorder.hpp"


class AudioBufferSource: public StreamSource {
	uint8_t* buffer = nullptr;
	uint64_t timeStart_us = 0;

protected:
	std::shared_ptr<AudioRecorder> recorder;
	AudioQueue *freeBufQueue_;  // Owner of the queue
	AudioQueue *recBufQueue_;   // Owner of the queue

public:
	AudioBufferSource() {
		SampleFormat sampleFormat;
		memset(&sampleFormat, 0, sizeof(sampleFormat));
		sampleFormat.pcmFormat_ = static_cast<uint16_t>(SL_PCMSAMPLEFORMAT_FIXED_16);

		// SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
		sampleFormat.channels_ = AUDIO_SAMPLE_CHANNELS;
		sampleFormat.sampleRate_ = static_cast<SLmilliHertz>(20000 * 1000) * 1000;
		sampleFormat.framesPerBuf_ = 24;//engine.fastPathFramesPerBuf_;
		recorder = std::make_shared<AudioRecorder>(&sampleFormat, nullptr); //engine.slEngineItf_);
		if (!recorder) {
			return;
		}
		recorder->SetBufQueues(freeBufQueue_, recBufQueue_);
		recorder->RegisterCallback(EngineService, nullptr);//(void *)&engine);
	}

	static bool EngineService(void *ctx, uint32_t msg, void *data) {
		switch (msg) {
			case ENGINE_SERVICE_MSG_RETRIEVE_DUMP_BUFS: {
//				*(static_cast<uint32_t *>(data)) = dbgEngineGetBufCount();
				break;
			}
			case ENGINE_SERVICE_MSG_RECORDED_AUDIO_AVAILABLE: {
				// adding audio delay effect
//				sample_buf *buf = static_cast<sample_buf *>(data);
//				assert(engine.fastPathFramesPerBuf_ ==
//					   buf->size_ / engine.sampleChannels_ / (engine.bitsPerSample_ / 8));
//				engine.delayEffect_->process(reinterpret_cast<int16_t *>(buf->buf_),
//											 engine.fastPathFramesPerBuf_);
				break;
			}
			default:
				assert(false);
		}

		return true;
	}

    virtual void start() {
		recorder->Start();
		loadNextSample();
	}

    virtual void stop() {
//		recorder->codec()->stop();
    }

    virtual void loadNextSample() {
		sample.clear();
    }

};
