#pragma once
#include "config.hpp"

// assert macro

#define AL_ASSERT(condition) \
    do { \
        if (!(condition)) [[unlikely]] { \
            ::AsyncLoad::_assertionFail(#condition, __FILE__, __LINE__); \
        } \
    } while (false)

#ifdef AL_DEBUG
# define AL_DEBUG_ASSERT(c) AL_ASSERT(c)
#else
# define AL_DEBUG_ASSERT(c) ((void)0)
#endif

namespace AsyncLoad {
    [[noreturn]] AL_DLL void _assertionFail(std::string_view what, std::string_view file, int line);
}
