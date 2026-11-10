#include "config.hpp"

#ifdef AsyncLoad_EXPORTS
# define GEODE_DEFINE_EVENT_EXPORTS
#else
# undef GEODE_DEFINE_EVENT_EXPORTS
#endif
#include <Geode/loader/EventExport.hpp>

#undef MY_MOD_ID
#define MY_MOD_ID "dankmeme.async-load-api"

#define AL_EVENT_EXPORT(...) GEODE_EVENT_EXPORT(__VA_ARGS__)
#define AL_EVENT_EXPORT_MEMBER(...) GEODE_EVENT_EXPORT_MEMBER(__VA_ARGS__)
#define AL_EVENT_EXPORT_MEMBER_STATIC(...) GEODE_EVENT_EXPORT_MEMBER_STATIC(__VA_ARGS__)
