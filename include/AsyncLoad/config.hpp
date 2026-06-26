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

