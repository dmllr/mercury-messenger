#pragma once

#ifdef ANDROID
#include <plog/Appenders/AndroidAppender.h>
#else
#include <plog/Appenders/ColorConsoleAppender.h>
#endif
#include <plog/Appenders/IAppender.h>
#include <plog/Formatters/FuncMessageFormatter.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Init.h>
#include <plog/Log.h>


constexpr auto logInstanceId = 454;
#define LGV PLOG_(logInstanceId, plog::verbose)  // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGD PLOG_(logInstanceId, plog::debug)    // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGI PLOG_(logInstanceId, plog::info)     // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGW PLOG_(logInstanceId, plog::warning)  // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)
#define LGE PLOG_(logInstanceId, plog::error)    // NOLINT(*-pro-type-reinterpret-cast,*-lambda-function-name)

struct Log {
  static constexpr auto tag = "eillance";
  static constexpr auto logLevel = plog::verbose;

  /**
   * @brief Initializes the logging system with a custom appender.
   *
   * This function should be called once at the beginning of the application
   * to configure the logging output. If not called, logs will be discarded.
   *
   * @param appender A pointer to the plog appender instance to be used for logging.
   * @param severity The minimum severity level to log.
   */
  // NOLINTNEXTLINE(*-default-arguments-declarations)
  static void Initialize(plog::IAppender* appender, plog::Severity severity = logLevel) { plog::init(severity, appender); }

  /**
   * @brief Initializes the logging system with the default platform-specific appender.
   *
   * This is a convenience function to set up logging with default settings
   * (console on desktop, logcat on Android).
   *
   * @param severity The minimum severity level to log.
   */
  // NOLINTNEXTLINE(*-default-arguments-declarations)
  static void InitializeDefault(plog::Severity severity = logLevel) {
#ifdef ANDROID
    static plog::AndroidAppender<plog::FuncMessageFormatter> default_appender{tag};
    plog::init(severity, &default_appender);
#else
    static plog::ColorConsoleAppender<plog::TxtFormatter> default_appender;
    plog::init(severity, &default_appender);
#endif
  }
};
