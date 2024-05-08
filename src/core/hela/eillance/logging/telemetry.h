#pragma once
//#define HE_TELEMETRY_ENABLED

#ifdef HE_TELEMETRY_ENABLED
#define TELEMETRY(code) code
#else
#define TELEMETRY(code)
#endif

#define TLM TELEMETRY
