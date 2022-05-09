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
//	rtc::binary accumulative_buffer = {};

protected:
//	std::shared_ptr<AudioRecorder> recorder;
//	AudioQueue *freeBufQueue_;  // Owner of the queue
//	AudioQueue *recBufQueue_;   // Owner of the queue

	std::unique_ptr<Recorder> recorder = nullptr;

    OpusEncoder* opus_encoder;

public:
	AudioBufferSource() {
		recorder = std::make_unique<Recorder>();
//		recorder->setRecordingDeviceId(0);

//		//
//		SLObjectItf slEngineObj_;
//		SLEngineItf slEngineItf_;
//		auto result = slCreateEngine(&slEngineObj_, 0, NULL, 0, NULL, NULL);
//		SLASSERT(result);
//
//		result = (*slEngineObj_)->Realize(slEngineObj_, SL_BOOLEAN_FALSE);
//		SLASSERT(result);
//
//		result = (*slEngineObj_)->GetInterface(slEngineObj_, SL_IID_ENGINE, &slEngineItf_);
//		SLASSERT(result);
//
//		// compute the RECOMMENDED fast audio buffer size:
//		//   the lower latency required
//		//     *) the smaller the buffer should be (adjust it here) AND
//		//     *) the less buffering should be before starting player AFTER
//		//        receiving the recorder buffer
//		//   Adjust the bufSize here to fit your bill [before it busts]
//		uint32_t bufSize = fastPathFramesPerBuf * AUDIO_CHANNELS * SL_PCMSAMPLEFORMAT_FIXED_16;
//		bufSize = (bufSize + 7) >> 3;  // bits --> byte
//		auto bufCount_ = BUF_COUNT;
//		auto bufs_ = allocateSampleBufs(bufCount_, bufSize);
//		assert(bufs_);
//
//		freeBufQueue_ = new AudioQueue(bufCount_);
//		recBufQueue_ = new AudioQueue(bufCount_);
//		assert(freeBufQueue_ && recBufQueue_);
//		for (uint32_t i = 0; i < bufCount_; i++) {
//			freeBufQueue_->push(&bufs_[i]);
//		}
//
//		//
//        SampleFormat sampleFormat;
//		memset(&sampleFormat, 0, sizeof(sampleFormat));
//		sampleFormat.pcmFormat_ = static_cast<uint16_t>(SL_PCMSAMPLEFORMAT_FIXED_16);
//		// SampleFormat.representation_ = SL_ANDROID_PCM_REPRESENTATION_SIGNED_INT;
//		sampleFormat.channels_ = AUDIO_CHANNELS;
//		sampleFormat.sampleRate_ = static_cast<SLmilliHertz>(44100) * 1000;
//		sampleFormat.framesPerBuf_ = fastPathFramesPerBuf;
//		recorder = std::make_shared<AudioRecorder>(&sampleFormat, slEngineItf_);
//		if (!recorder) {
//			return;
//		}
//		recorder->SetBufQueues(freeBufQueue_, recBufQueue_);
//		recorder->RegisterCallback(recorderCallback, this);

		// opus
        auto error = 0;
		opus_encoder = opus_encoder_create(24000, AUDIO_CHANNELS, OPUS_APPLICATION_AUDIO, &error);
		assert(error == OPUS_OK);
		assert(opus_encoder != nullptr);
	}

//	static bool recorderCallback(void *ctx, uint32_t msg, void *data) {
//		switch (msg) {
//			case RECORDER_CALLBACK_MSG_RETRIEVE_DUMP_BUFS: {
//				*(static_cast<uint32_t*>(data)) = 24000/200; // todo ????
//				break;
//			}
//			case RECORDER_CALLBACK_MSG_RECORDED_AUDIO_AVAILABLE: {
//				auto pcm_buf = static_cast<sample_buf*>(data);
//				auto ths = reinterpret_cast<AudioBufferSource*>(ctx);
//				auto size = opus_encode(ths->opus_encoder, pcm_buf->buf_, pcm_buf->size_, ths->encoded_buffer.data(), ths->encoded_buffer.size());
//				if (likely(size > 0)) {
//					ths->accumulative_buffer.insert(
//							ths->accumulative_buffer.end(),
//							reinterpret_cast<std::byte *>(&ths->encoded_buffer[0]),
//							reinterpret_cast<std::byte *>(&ths->encoded_buffer[size])
//					);
//				}
//				break;
//			}
//			default:
//				assert(false);
//		}
//
//		return true;
//	}

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
