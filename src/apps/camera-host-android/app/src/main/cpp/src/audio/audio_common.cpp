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

#include "audio_common.h"
#include "logging/logging.h"
#include <string>

static const int32_t audioFormatEnum[] = {
    AAUDIO_FORMAT_INVALID,
    AAUDIO_FORMAT_UNSPECIFIED,
    AAUDIO_FORMAT_PCM_I16,
    AAUDIO_FORMAT_PCM_FLOAT,
};
static const int32_t audioFormatCount = sizeof(audioFormatEnum) /
                                        sizeof(audioFormatEnum[0]);

static const uint32_t sampleFormatBPP[] = {
    0xffff,
    0xffff,
    16,  //I16
    32,  //FLOAT
};
uint16_t SampleFormatToBpp(aaudio_format_t format) {
  for (int32_t i = 0; i < audioFormatCount; ++i) {
    if (audioFormatEnum[i] == format)
      return sampleFormatBPP[i];
  }
  return 0xffff;
}
static const char* audioFormatStr[] = {
    "AAUDIO_FORMAT_INVALID",      // = -1,
    "AAUDIO_FORMAT_UNSPECIFIED",  // = 0,
    "AAUDIO_FORMAT_PCM_I16",
    "AAUDIO_FORMAT_PCM_FLOAT",
};
const char* FormatToString(aaudio_format_t format) {
  for (int32_t i = 0; i < audioFormatCount; ++i) {
    if (audioFormatEnum[i] == format)
      return audioFormatStr[i];
  }
  return "UNKNOW_AUDIO_FORMAT";
}

void PrintAudioStreamInfo(const AAudioStream* stream) {
#define STREAM_CALL(c) AAudioStream_##c((AAudioStream*)stream)
  LOGI << "StreamID: " << stream;
  LOGI << "BufferCapacity: " << STREAM_CALL(getBufferCapacityInFrames);
  LOGI << "BufferSize: " << STREAM_CALL(getBufferSizeInFrames);
  LOGI << "FramesPerBurst: " << STREAM_CALL(getFramesPerBurst);
  LOGI << "XRunCount: " << STREAM_CALL(getXRunCount);
  LOGI << "SampleRate: " << STREAM_CALL(getSampleRate);
  LOGI << "SamplesPerFrame: " << STREAM_CALL(getChannelCount);
  LOGI << "DeviceId: " << STREAM_CALL(getDeviceId);
  LOGI << "Format: " << FormatToString(STREAM_CALL(getFormat));
  LOGI << "SharingMode: " << ((STREAM_CALL(getSharingMode)) == AAUDIO_SHARING_MODE_EXCLUSIVE ? "EXCLUSIVE" : "SHARED");

  aaudio_performance_mode_t perfMode = STREAM_CALL(getPerformanceMode);
  std::string perfModeDescription;
  switch (perfMode) {
    case AAUDIO_PERFORMANCE_MODE_NONE:
      perfModeDescription = "NONE";
      break;
    case AAUDIO_PERFORMANCE_MODE_LOW_LATENCY:
      perfModeDescription = "LOW_LATENCY";
      break;
    case AAUDIO_PERFORMANCE_MODE_POWER_SAVING:
      perfModeDescription = "POWER_SAVING";
      break;
    default:
      perfModeDescription = "UNKNOWN";
      break;
  }
  LOGI << "PerformanceMode: " << perfModeDescription.c_str();

  aaudio_direction_t dir = STREAM_CALL(getDirection);
  LOGI << "Direction: " << (dir == AAUDIO_DIRECTION_OUTPUT ? "OUTPUT" : "INPUT");
  if (dir == AAUDIO_DIRECTION_OUTPUT) {
    LOGI << "FramesReadByDevice: " << (int32_t)STREAM_CALL(getFramesRead);
    LOGI << "FramesWriteByApp: " << (int32_t)STREAM_CALL(getFramesWritten);
  } else {
    LOGI << "FramesReadByApp: " << (int32_t)STREAM_CALL(getFramesRead);
    LOGI << "FramesWriteByDevice: " << (int32_t)STREAM_CALL(getFramesWritten);
  }
#undef STREAM_CALL
}

int64_t timestamp_to_nanoseconds(timespec ts) {
  return (ts.tv_sec * (int64_t)NANOS_PER_SECOND) + ts.tv_nsec;
}

int64_t get_time_nanoseconds(clockid_t clockid) {
  timespec ts{};
  clock_gettime(clockid, &ts);
  return timestamp_to_nanoseconds(ts);
}

void ConvertMonoToStereo(int16_t* buffer, int32_t numFrames) {
  for (int i = numFrames - 1; i >= 0; i--) {
    buffer[i * 2] = buffer[i];
    buffer[(i * 2) + 1] = buffer[i];
  }
}
