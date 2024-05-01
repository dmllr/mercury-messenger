#pragma once

#include "hela/eillance/capture/audio_encoder_libopus.hpp"

/*
 * Available options:
 *
 * Encoding powered by ffmpeg, currently fails due to unknown segmentation fault
 *   #include "hela/eillance/capture/audio_encoder_libffmpeg.hpp"
 *
 * Encoding powered by libopus, seems working fine
 *   #include "hela/eillance/capture/audio_encoder_libopus.hpp"
*/
