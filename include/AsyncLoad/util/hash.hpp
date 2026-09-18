#include "config.hpp"
#include <stdint.h>

namespace AsyncLoad {

constexpr uint32_t FNV_OFFSET_BASIS = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

constexpr static inline uint32_t _fnv1a_hash(const char *str, uint32_t hash = FNV_OFFSET_BASIS) {
    return (*str == '\0') ? hash : _fnv1a_hash(str + 1, (hash ^ static_cast<uint8_t>(*str)) * FNV_PRIME);
}

#define AL_STRING_HASH(x) (::AsyncLoad::_fnv1a_hash(x))

static uint32_t hashStringRuntime(const char* str) {
    uint32_t hash = FNV_OFFSET_BASIS;

    while (*str) {
        hash = ((hash ^ static_cast<uint8_t>(*str++)) * FNV_PRIME);
    }

    return hash;
}

}