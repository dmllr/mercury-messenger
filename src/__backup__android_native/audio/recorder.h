#ifndef AAUDIO_ECHOAUDIOENGINE_H
#define AAUDIO_ECHOAUDIOENGINE_H

#include "audio_common.h"
#include <thread>

class Recorder {

public:
  static constexpr auto channelCount = kMonoChannelCount;
  static constexpr auto sampleRate = 24000;
  static constexpr auto framesPerBurst = sampleRate / 50;  // todo

  ~Recorder();
  void setRecordingDeviceId(int32_t deviceId);

  aaudio_result_t read(void* audioData, int32_t numFrames);

  void start();
  void stop();
  void restart();

  void errorCallback(AAudioStream* stream, aaudio_result_t __unused error);

private:
  bool isFirstDataCallback_ = true;
  aaudio_format_t format_ = AAUDIO_FORMAT_PCM_FLOAT;
  int32_t recordingDeviceId_ = AAUDIO_UNSPECIFIED;
  AAudioStream* recordingStream_ = nullptr;
  std::mutex restartingLock_;

  void openRecordingStream();
  void drainRecordingStream(void* audioData, int32_t numFrames);

  void startStream(AAudioStream* stream);
  void stopStream(AAudioStream* stream);
  void closeStream(AAudioStream* stream);

  AAudioStreamBuilder* createStreamBuilder();

  void setupRecordingStreamParameters(AAudioStreamBuilder* builder);
  static void warnIfNotLowLatency(AAudioStream* stream);
};

#endif  //AAUDIO_ECHOAUDIOENGINE_H
