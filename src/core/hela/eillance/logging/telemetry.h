#pragma once
#ifndef NDEBUG
#define HE_TELEMETRY_ENABLED
#endif

#ifdef HE_TELEMETRY_ENABLED
namespace hela::eillance {

static constexpr auto TelemetryInterval = 5;  // s

}

#define TELEMETRY(code) code
#else
#define TELEMETRY(code)
#endif

#define TLM TELEMETRY
