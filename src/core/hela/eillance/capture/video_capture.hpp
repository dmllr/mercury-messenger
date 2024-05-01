#pragma once

#ifdef __ANDROID__
#include "hela/eillance/capture/video_capture_android.hpp"
#else
#include "hela/eillance/capture/video_capture_general.hpp"
#endif
