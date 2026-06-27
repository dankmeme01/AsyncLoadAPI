#pragma once

#include "config.hpp"
#include "assert.hpp"
#include <Geode/utils/general.hpp>

#ifdef AL_DEBUG
# define AL_TRACE(...) log::debug(__VA_ARGS__)
#else
# define AL_TRACE(...) do {} while (0)
#endif

namespace AsyncLoad {

AL_DLL std::optional<cocos2d::CCRect> parseRect(std::string_view str);
AL_DLL std::optional<std::pair<float, float>> parseVec2(std::string_view str);

inline std::optional<cocos2d::CCPoint> parsePoint(std::string_view str) {
    return parseVec2(str).transform([](auto&& pair) {
        return cocos2d::CCPoint { pair.first, pair.second };
    });
}

inline std::optional<cocos2d::CCSize> parseSize(std::string_view str) {
    return parseVec2(str).transform([](auto&& pair) {
        return cocos2d::CCSize { pair.first, pair.second };
    });
}

constexpr uint32_t FNV_OFFSET_BASIS = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

constexpr static inline uint32_t _fnv1a_hash(const char *str, uint32_t hash = FNV_OFFSET_BASIS) {
    return (*str == '\0') ? hash : _fnv1a_hash(str + 1, (hash ^ static_cast<uint8_t>(*str)) * FNV_PRIME);
}

#define STRING_HASH(x) (::AsyncLoad::_fnv1a_hash(x))

static uint32_t hashStringRuntime(const char* str) {
    uint32_t hash = FNV_OFFSET_BASIS;

    while (*str) {
        hash = ((hash ^ static_cast<uint8_t>(*str++)) * FNV_PRIME);
    }

    return hash;
}

template <typename T>
struct Atomic : std::atomic<T> {
    Atomic() : std::atomic<T>() {}
    Atomic(T value) : std::atomic<T>(value) {}

    Atomic(const Atomic& other) : std::atomic<T>(other.load(std::memory_order::acquire)) {}

    Atomic& operator=(const Atomic& other) {
        if (this != &other) {
            this->store(other.load(std::memory_order::acquire), std::memory_order::release);
        }
        return *this;
    }

    Atomic(Atomic&& other) noexcept : std::atomic<T>(other.load(std::memory_order::acquire)) {}

    Atomic& operator=(Atomic&& other) noexcept {
        if (this != &other) {
            this->store(other.load(std::memory_order::acquire), std::memory_order::release);
        }
        return *this;
    }
};


}
