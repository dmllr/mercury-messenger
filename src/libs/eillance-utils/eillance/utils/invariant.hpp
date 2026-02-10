#pragma once

#include <exception>
#include <stdexcept>

#if defined(_MSC_VER) && !defined(__clang__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define UNREACHABLE() __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define UNREACHABLE() __builtin_unreachable()
#else
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define UNREACHABLE() std::terminate()
#endif

// In debug builds, invariants throw exceptions with messages.
// In release builds, they are hints to the optimizer that the condition is always true.
#if defined(DEBUG) || !defined(NDEBUG)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INVARIANT(condition)                                                                       \
  do { /* NOLINT(*-avoid-do-while) */                                                              \
    if (!(condition)) {                                                                            \
      /* NOLINTNEXTLINE(*-vararg) */                                                               \
      std::printf("%s:%d in %s(): Invariant violation " #condition, __FILE__, __LINE__, __func__); \
      throw std::logic_error("Invariant violated: " #condition);                                   \
    }                                                                                              \
  } while (0)
#else
#define INVARIANT(condition)          \
  do { /* NOLINT(*-avoid-do-while) */ \
    if (!(condition)) {               \
      UNREACHABLE();                  \
    }                                 \
  } while (0)
#endif

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define INVARIANT_NOT_NULL(arg) INVARIANT((arg) != nullptr)
