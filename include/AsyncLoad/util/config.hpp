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


#ifdef AL_DEBUG
# define AL_TRACE(...) log::debug(__VA_ARGS__)
# define AL_TRACE_NOISY(...) log::trace(__VA_ARGS__)
#else
# define AL_TRACE(...) do {} while (0)
# define AL_TRACE_NOISY(...) do {} while (0)
#endif

// Macro for things that are unavailable in the Event API
#if defined ASYNCLOAD_LINKING || defined AsyncLoad_EXPORTS
# define AL_CAN_LINK
#endif