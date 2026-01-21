#pragma once

#include <exception>
#include <stdexcept>

#if defined(_MSC_VER) && !defined(__clang__)
#define UNREACHABLE() __assume(0)
#elif defined(__GNUC__) || defined(__clang__)
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE() std::terminate()  // Fallback for unsupported compilers
#endif

// In debug builds, invariants throw exceptions with messages.
// In release builds, they are hints to the optimizer that the condition is always true.
#if defined(DEBUG) || !defined(NDEBUG)
    #define INVARIANT(condition) \
        do {  /* NOLINT(*-avoid-do-while) */ \
            if (!(condition)) { \
                throw std::logic_error("Invariant violated: " #condition); \
            } \
        } while(0)
#else
    #define INVARIANT(condition) \
        do {  /* NOLINT(*-avoid-do-while) */ \
            if (!(condition)) { \
                UNREACHABLE(); \
            } \
        } while(0)
#endif

#define INVARIANT_NOT_NULL(arg) INVARIANT((arg) != nullptr)
