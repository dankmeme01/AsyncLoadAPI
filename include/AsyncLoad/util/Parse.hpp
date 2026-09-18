#pragma once
#include "config.hpp"
#include <string_view>
#include <utility>
#include <optional>
#include <cocos2d.h>

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

}
