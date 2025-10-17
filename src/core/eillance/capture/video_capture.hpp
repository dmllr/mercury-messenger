#pragma once

#ifdef __ANDROID__
#include "eillance/capture/video_capture_android.hpp"
#else
#include "eillance/capture/video_capture_general.hpp"
#endif
