#pragma once

#if defined(_WIN32) && defined(MECRAFT_SHARED)
#  if defined(MECRAFT_CORE_BUILD)
#    define MECRAFT_API __declspec(dllexport)
#  else
#    define MECRAFT_API __declspec(dllimport)
#  endif
#else
#  define MECRAFT_API
#endif
