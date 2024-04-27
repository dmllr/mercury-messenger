#pragma once

#include <cstdint>

#include "hela/eillance/utils/units.hpp"

#ifdef _MSC_VER
  // taken from https://stackoverflow.com/questions/10905892/equivalent-of-gettimeday-for-windows
  #include <WinSock2.h>  // for struct timeval
  #include <Windows.h>

namespace {

struct timezone {
  int tz_minuteswest;
  int tz_dsttime;
};

auto gettimeofday(struct timeval* tv, struct timezone* tz) -> int {
  if (tv) {
    FILETIME filetime; /* 64-bit value representing the number of 100-nanosecond intervals since
		                      January 1, 1601 00:00 UTC */
    ULARGE_INTEGER x;
    ULONGLONG usec;
    static const ULONGLONG epoch_offset_us =
        11644473600000000ULL; /* microseconds between Jan 1,1601 and Jan 1,1970 */

  #if _WIN32_WINNT >= _WIN32_WINNT_WIN8
    GetSystemTimePreciseAsFileTime(&filetime);
  #else
    GetSystemTimeAsFileTime(&filetime);
  #endif
    x.LowPart = filetime.dwLowDateTime;
    x.HighPart = filetime.dwHighDateTime;
    usec = x.QuadPart / 10 - epoch_offset_us;
    tv->tv_sec = static_cast<long>(usec / Units::Microseconds);
    tv->tv_usec = static_cast<long>(usec % Units::Microseconds);
  }
  if (tz) {
    TIME_ZONE_INFORMATION timezone;
    GetTimeZoneInformation(&timezone);
    tz->tz_minuteswest = timezone.Bias;
    tz->tz_dsttime = 0;
  }
  return 0;
}

}  // namespace
#else
  #include <sys/time.h>
#endif

inline auto currentTimeInMicroSeconds() -> std::uint64_t {
  struct timeval time {};
  gettimeofday(&time, nullptr);
  return std::uint64_t(time.tv_sec) * Units::Microseconds + time.tv_usec;
}
