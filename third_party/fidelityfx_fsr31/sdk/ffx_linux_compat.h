#ifndef MECRAFT_FFX_LINUX_COMPAT_H
#define MECRAFT_FFX_LINUX_COMPAT_H

#if defined(__linux__)

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <locale>
#include <cmath>

#ifndef FFX_UNUSED
#define FFX_UNUSED(value) (void)(value)
#endif

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

using std::floor;
using std::log2;

template <size_t Capacity>
int wcscpy_s(wchar_t (&destination)[Capacity], const wchar_t* source) {
    if (source == nullptr) {
        destination[0] = L'\0';
        return EINVAL;
    }
    const size_t length = std::wcslen(source);
    if (length >= Capacity) {
        destination[0] = L'\0';
        return ERANGE;
    }
    std::wmemcpy(destination, source, length + 1u);
    return 0;
}

inline int wcscpy_s(wchar_t* destination,
                    const size_t capacity,
                    const wchar_t* source) {
    if (destination == nullptr || source == nullptr || capacity == 0u) {
        return EINVAL;
    }
    const size_t length = std::wcslen(source);
    if (length >= capacity) {
        destination[0] = L'\0';
        return ERANGE;
    }
    std::wmemcpy(destination, source, length + 1u);
    return 0;
}

inline int strcpy_s(char* destination,
                    const size_t capacity,
                    const char* source) {
    if (destination == nullptr || source == nullptr || capacity == 0u) {
        return EINVAL;
    }
    const size_t length = std::strlen(source);
    if (length >= capacity) {
        destination[0] = '\0';
        return ERANGE;
    }
    std::memcpy(destination, source, length + 1u);
    return 0;
}

inline int wcstombs_s(size_t* converted,
                      char* destination,
                      const size_t capacity,
                      const wchar_t* source,
                      const size_t count) {
    if (converted == nullptr || destination == nullptr || source == nullptr ||
        capacity == 0u) {
        return EINVAL;
    }
    const size_t limit = std::min(capacity - 1u, count);
    const size_t result = std::wcstombs(destination, source, limit);
    if (result == static_cast<size_t>(-1)) {
        destination[0] = '\0';
        *converted = 0u;
        return EILSEQ;
    }
    destination[result] = '\0';
    *converted = result + 1u;
    return result < capacity ? 0 : ERANGE;
}

template <typename... Args>
int sprintf_s(char* destination,
              const size_t capacity,
              const char* format,
              Args... args) {
    return std::snprintf(destination, capacity, format, args...);
}

template <typename... Args>
int swprintf_s(wchar_t* destination,
               const size_t capacity,
               const wchar_t* format,
               Args... args) {
    return std::swprintf(destination, capacity, format, args...);
}

#endif // defined(__linux__)

#endif // MECRAFT_FFX_LINUX_COMPAT_H
