#include <media/NdkMediaCodec.h>
#include <utils/likely.h>


class CameraFramedSource: public FramedSource {
private:
	AMediaCodec* codec;
	AMediaCodecBufferInfo info;
	uint8_t* buffer = nullptr;
	size_t size = 0;
	ssize_t index;

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
		index = AMediaCodec_dequeueOutputBuffer(codec, &info, 100000);  // TODO const timeout
		if (likely(index >= 0)) {
			buffer = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(index), &size);
			AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(index), false);
		} else if (index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
			Log::i("Output buffers changed");
		} else if (index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			auto format = AMediaCodec_getOutputFormat(codec);
			Log::i("Format set to: %s", AMediaFormat_toString(format));
			AMediaFormat_delete(format);
		} else if (index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
			Log::i("No output buffer right now");
		} else {
			Log::w("AMediaCodec_dequeueOutputBuffer unexpected return: %d", index);
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
