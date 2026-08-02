#pragma once

#ifdef _WIN32
# ifdef AsyncLoad_EXPORTS
#  define AL_DLL __declspec(dllexport)
# else
#  define AL_DLL __declspec(dllimport)
# endif
#else
# ifdef AsyncLoad_EXPORTS
#  define AL_DLL __attribute__((visibility("default")))
# else
#  define AL_DLL
# endif
#endif


// various macros
#ifdef __clang__
# define AL_RESTRICT __restrict__
#else
# define AL_RESTRICT __restrict
#endif
