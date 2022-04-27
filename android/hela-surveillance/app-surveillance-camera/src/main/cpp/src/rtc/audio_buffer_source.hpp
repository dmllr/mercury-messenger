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
		//
		SLObjectItf slEngineObj_;
		SLEngineItf slEngineItf_;
		auto result = slCreateEngine(&slEngineObj_, 0, NULL, 0, NULL, NULL);
		SLASSERT(result);

		result = (*slEngineObj_)->Realize(slEngineObj_, SL_BOOLEAN_FALSE);
		SLASSERT(result);

		result = (*slEngineObj_)->GetInterface(slEngineObj_, SL_IID_ENGINE, &slEngineItf_);
		SLASSERT(result);

		// compute the RECOMMENDED fast audio buffer size:
		//   the lower latency required
		//     *) the smaller the buffer should be (adjust it here) AND
		//     *) the less buffering should be before starting player AFTER
		//        receiving the recorder buffer
		//   Adjust the bufSize here to fit your bill [before it busts]
		auto fastPathFramesPerBuf = 976; // todo real
		uint32_t bufSize = fastPathFramesPerBuf * AUDIO_SAMPLE_CHANNELS * SL_PCMSAMPLEFORMAT_FIXED_16;
		bufSize = (bufSize + 7) >> 3;  // bits --> byte
		auto bufCount_ = BUF_COUNT;
		auto bufs_ = allocateSampleBufs(bufCount_, bufSize);
		assert(bufs_);

		freeBufQueue_ = new AudioQueue(bufCount_);
		recBufQueue_ = new AudioQueue(bufCount_);
		assert(freeBufQueue_ && recBufQueue_);
		for (uint32_t i = 0; i < bufCount_; i++) {
			freeBufQueue_->push(&bufs_[i]);
		}
		
		//
        SampleFormat sampleFormat;
//		memset(&sampleFormat, 0, sizeof(sampleFormat));
		sampleFormat.pcmFormat_ = static_cast<uint16_t>(SL_PCMSAMPLEFORMAT_FIXED_16);
		// SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
		sampleFormat.channels_ = AUDIO_SAMPLE_CHANNELS;
		sampleFormat.sampleRate_ = static_cast<SLmilliHertz>(44100) * 1000;
		sampleFormat.framesPerBuf_ = fastPathFramesPerBuf;
		recorder = std::make_shared<AudioRecorder>(&sampleFormat, slEngineItf_);
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
//				assert(fastPathFramesPerBuf_ ==
//					   buf->size_ / sampleChannels_ / (bitsPerSample_ / 8));
//				delayEffect_->process(reinterpret_cast<int16_t *>(buf->buf_),
//											 fastPathFramesPerBuf_);
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
