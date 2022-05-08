/*
 * Copyright 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AAUDIO_ECHOAUDIOENGINE_H
#define AAUDIO_ECHOAUDIOENGINE_H

#include <thread>
#include "audio_common.h"

class Recorder {

public:
    ~Recorder();
    void setRecordingDeviceId(int32_t deviceId);

    aaudio_result_t read(void *audioData, int32_t numFrames);

    void start();
    void stop();
    void restart();

    void errorCallback(AAudioStream *stream, aaudio_result_t  __unused error);

private:
    bool isFirstDataCallback_ = true;
    int32_t recordingDeviceId_ = AAUDIO_UNSPECIFIED;
    aaudio_format_t format_ = AAUDIO_FORMAT_PCM_FLOAT;
    int32_t sampleRate_;
    int32_t inputChannelCount_ = kMonoChannelCount;
    AAudioStream *recordingStream_ = nullptr;
    int32_t framesPerBurst_;
    std::mutex restartingLock_;

    void openRecordingStream();
    void drainRecordingStream(void *audioData, int32_t numFrames);

    void startStream(AAudioStream* stream);
    void stopStream(AAudioStream* stream);
    void closeStream(AAudioStream* stream);

    AAudioStreamBuilder* createStreamBuilder();

    void setupRecordingStreamParameters(AAudioStreamBuilder *builder);
    static void warnIfNotLowLatency(AAudioStream *stream);
};

#endif //AAUDIO_ECHOAUDIOENGINE_H
