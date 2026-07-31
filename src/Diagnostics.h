#pragma once

#include <cstdio>

#if defined(MECRAFT_ENABLE_CONSOLE_OUTPUT)
#define MECRAFT_LOG_PRINTF(...)                                                                                        \
    do {                                                                                                               \
        std::printf(__VA_ARGS__);                                                                                      \
    } while (false)
#define MECRAFT_LOG_FPRINTF(...)                                                                                       \
    do {                                                                                                               \
        std::fprintf(__VA_ARGS__);                                                                                     \
    } while (false)
#define MECRAFT_LOG_FLUSH(stream)                                                                                      \
    do {                                                                                                               \
        std::fflush(stream);                                                                                           \
    } while (false)
#define MECRAFT_LOG_STREAM(expr)                                                                                       \
    do {                                                                                                               \
        expr;                                                                                                          \
    } while (false)
#else
#define MECRAFT_LOG_PRINTF(...)                                                                                        \
    do {                                                                                                               \
        if (false) {                                                                                                   \
            std::printf(__VA_ARGS__);                                                                                  \
        }                                                                                                              \
    } while (false)
#define MECRAFT_LOG_FPRINTF(...)                                                                                       \
    do {                                                                                                               \
        if (false) {                                                                                                   \
            std::fprintf(__VA_ARGS__);                                                                                 \
        }                                                                                                              \
    } while (false)
#define MECRAFT_LOG_FLUSH(stream)                                                                                      \
    do {                                                                                                               \
        if (false) {                                                                                                   \
            std::fflush(stream);                                                                                       \
        }                                                                                                              \
    } while (false)
#define MECRAFT_LOG_STREAM(expr)                                                                                       \
    do {                                                                                                               \
        if (false) {                                                                                                   \
            expr;                                                                                                      \
        }                                                                                                              \
    } while (false)
#endif
