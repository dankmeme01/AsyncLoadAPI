#pragma once
#include <Geode/utils/cocos.hpp>
#include <cocos2d.h>
#include <span>
#include <stdint.h>
#include "util/config.hpp"

namespace AsyncLoad {

/// Loads a CCBMFontConfiguration from the given data of an .fnt file. Returns null if loading failed.
/// This is thread-safe, and the returned configuration is not autoreleased, so you have the single reference to it.
/// This function does not use any cache
AL_DLL geode::Ref<cocos2d::CCBMFontConfiguration> loadFont(std::span<const uint8_t> data);

/// Loads a CCBMFontConfiguration from the given .fnt file. Returns null if loading failed.
/// This is thread-safe, and the returned configuration is not autoreleased, so you have the single reference to it.
/// If the `useCache` parameter is true, then this is not thread safe due to geode cache being used.
AL_DLL geode::Ref<cocos2d::CCBMFontConfiguration> loadFont(geode::ZStringView path, bool useCache = false);

}
