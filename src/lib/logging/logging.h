#pragma once

#include <string>

#include "likely.h"

#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Formatters/FuncMessageFormatter.h>
#include <plog/Formatters/TxtFormatter.h>
#ifdef ANDROID
#include <plog/Appenders/AndroidAppender.h>
#else
#include <plog/Appenders/ColorConsoleAppender.h>
#endif


struct Log {
	static constexpr auto tag = "helaeillance";
	static constexpr auto log_level = plog::verbose;


#ifdef ANDROID
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
#endif

private:
#ifdef ANDROID
	inline static plog::AndroidAppender<plog::FuncMessageFormatter> appender{tag};
#else
	inline static plog::ColorConsoleAppender<plog::TxtFormatter> appender;
#endif
	inline const static plog::Logger<0>& logger = plog::init(log_level, &appender);
};
