#pragma once

#include <string>

#include "eillance/utils/likely.h"

#include <plog/Appenders/IAppender.h>
#include <plog/Formatters/FuncMessageFormatter.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>
#ifdef ANDROID
#include <plog/Appenders/AndroidAppender.h>
#else
#include <plog/Appenders/ColorConsoleAppender.h>
#endif


struct Log {
  static constexpr auto tag = "eillance";
  static constexpr auto log_level = plog::verbose;

  /**
   * @brief Initializes the logging system with a custom appender.
   *
   * This function should be called once at the beginning of the application
   * to configure the logging output. If not called, logs will be discarded.
   *
   * @param appender A pointer to the plog appender instance to be used for logging.
   * @param severity The minimum severity level to log.
   */
  static void Initialize(plog::IAppender* appender, plog::Severity severity = log_level) {
    plog::init(severity, appender);
  }

  /**
   * @brief Initializes the logging system with the default platform-specific appender.
   *
   * This is a convenience function to set up logging with default settings
   * (console on desktop, logcat on Android).
   *
   * @param severity The minimum severity level to log.
   */
  static void InitializeDefault(plog::Severity severity = log_level) {
#ifdef ANDROID
    static plog::AndroidAppender<plog::FuncMessageFormatter> default_appender{tag};
    plog::init(severity, &default_appender);
#else
    static plog::ColorConsoleAppender<plog::TxtFormatter> default_appender;
    plog::init(severity, &default_appender);
#endif
  }


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

  template<typename... T>
  static void w(std::string_view fmt, T... p) {
    log_print(ANDROID_LOG_WARN, fmt.data(), p...);
  }

  template<typename... T>
  static void e(std::string_view fmt, T... p) {
    log_print(ANDROID_LOG_ERROR, fmt.data(), p...);
  }

  template<typename... T>
  static void v(std::string_view fmt, T... p) {
    log_print(ANDROID_LOG_VERBOSE, fmt.data(), p...);
  }

  template<typename... T>
  static void assert_or_die(bool cond, std::string_view fmt, T... p) {
    if (unlikely(!cond)) {
      log_print(ANDROID_LOG_FATAL, fmt.data(), p...);
      std::__throw_runtime_error(fmt.data());
    }
  }

  template<typename... T>
  static void assert_soft(bool cond, std::string_view fmt, T... p) {
    if (unlikely(!cond))
      log_print(ANDROID_LOG_ERROR, fmt.data(), p...);
  }
#endif
};
