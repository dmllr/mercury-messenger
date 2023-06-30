#include <media/NdkMediaCodec.h>
#include <utils/likely.h>


class CameraFramedSource: public FramedSource {
private:
	AMediaCodec* codec;
	AMediaCodecBufferInfo info;
	uint8_t* buffer = nullptr;
	size_t size = 0;

public:
	CameraFramedSource(UsageEnvironment& env) : FramedSource(env) { }

	static CameraFramedSource* createNew(UsageEnvironment& env, AMediaCodec* codec) {
		auto framedSource = new CameraFramedSource(env);
		framedSource->codec = codec;

		return framedSource;
	}

	~CameraFramedSource() {
		Medium::close(this);
	}

	void doGetNextFrame() override {
		const auto index = AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);  // TODO const timeout
		if (likely(index >= 0)) {
			buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), &size);
			AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
		} else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
			LOGI << "Output buffers changed";
		} else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			auto format = AMediaCodec_getOutputFormat(codec);
			LOGI << "Format set to: %s", AMediaFormat_toString(format);
			AMediaFormat_delete(format);
		} else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
			LOGI << "No output buffer right now";
		} else {
			LOGI << "AMediaCodec_dequeueOutputBuffer unexpected return: " << index;
		}

		deliver();
	}

	void deliver() {
		if (unlikely(!isCurrentlyAwaitingData()))
			// not ready for the data yet
			return;

		auto newFrameDataStart = buffer;
		unsigned newFrameSize = size;

		if (likely(newFrameSize <= fMaxSize)) {
			fFrameSize = newFrameSize;
		} else {
			fFrameSize = fMaxSize;
			fNumTruncatedBytes = newFrameSize - fMaxSize;
		}

		gettimeofday(&fPresentationTime, NULL);
		memmove(fTo, newFrameDataStart, fFrameSize);

		FramedSource::afterGetting(this);
	}

	Boolean isH264VideoStreamFramer() const override {
		return True;
	}
};
