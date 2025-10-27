#pragma once
#ifndef NDEBUG
#define HE_TELEMETRY_ENABLED
#endif

#ifdef HE_TELEMETRY_ENABLED
namespace eillance {

static constexpr auto telemetry_interval = 5;  // s

}

#define TELEMETRY(code) code
#else
#define TELEMETRY(code)
#endif

#define TLM TELEMETRY
