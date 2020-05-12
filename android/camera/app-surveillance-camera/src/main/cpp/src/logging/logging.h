#pragma once

#include <string>
#include <android/log.h>
#include "utils/likely.h"


struct Log {
	static constexpr auto tag = "LESC-CAMERA";

	static void log_print(const android_LogPriority priority, const char* fmt, ...) {
		va_list ap;
		va_start(ap, fmt);
		__android_log_vprint(priority, tag, fmt, ap);
		va_end(ap);
	}

	template<typename... T>
	static void i(std::string_view fmt, T... p) {
		log_print(ANDROID_LOG_INFO, fmt.data(), p...);
	}

	template<typename... T>
	static void d(std::string_view fmt, T... p) {
		log_print(ANDROID_LOG_DEBUG, fmt.data(), p...);
	}

	template<typename ...T>
	static void w(std::string_view fmt, T... p) {
		log_print(ANDROID_LOG_WARN, fmt.data(), p...);
	}

	template<typename ...T>
	static void e(std::string_view fmt, T... p) {
		log_print(ANDROID_LOG_ERROR, fmt.data(), p...);
	}

	template<typename ...T>
	static void v(std::string_view fmt, T... p) {
		log_print(ANDROID_LOG_VERBOSE, fmt.data(), p...);
	}

	template<typename ...T>
	static void assert_or_die(bool cond, std::string_view fmt, T... p) {
		if (unlikely(!cond)) {
			log_print(ANDROID_LOG_FATAL, fmt.data(), p...);
			std::__throw_runtime_error(fmt.data());
		}
	}

	template<typename ...T>
	static void assert_soft(bool cond, std::string_view fmt, T... p) {
		if (unlikely(!cond))
			log_print(ANDROID_LOG_ERROR, fmt.data(), p...);
	}
};
