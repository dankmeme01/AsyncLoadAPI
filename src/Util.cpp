#include <AsyncLoad/Util.hpp>
#include <asp/iter.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

std::optional<CCRect> parseRect(std::string_view str) {
    // A rect is formatted as {{x,y},{w,h}}.
    // We are going to go by the following rules:
    // * First character has to be an opening brace, and last character has to be a closing brace
    // * We find the index of the second comma, and split the string into two substrings (omitting the very first and very last braces)
    // * Call parseCCPoint on both substrings and use their results

    CCPoint origin;
    CCSize size;

    if (str[0] != '{' || str[str.size() - 1] != '}') {
        return std::nullopt;
    }

    size_t secondCommaIdx = 0;
    bool isFirstComma = true;
    for (size_t i = 0; i < str.size(); i++) {
        char c = str[i];
        if (c == ',') {
            if (isFirstComma) {
                isFirstComma = false;
            } else {
                secondCommaIdx = i;
                break;
            }
        }
    }

    if (secondCommaIdx == 0) {
        return std::nullopt;
    }

    auto originStr = str.substr(1, secondCommaIdx - 1);
    auto sizeStr = str.substr(secondCommaIdx + 1, str.size() - secondCommaIdx - 2);

    if (auto x = parsePoint(originStr)) {
        origin = x.value();
    } else {
        return std::nullopt;
    }

    if (auto x = parseSize(sizeStr)) {
        size = x.value();
    } else {
        return std::nullopt;
    }

    return CCRect{origin, size};
}

std::optional<std::pair<float, float>> parseVec2(std::string_view str) {
    // A point is formatted in form {x,y}.
    // Cocos does a bunch of unnecessary checks here, we are just going to go by the following rules:
    // * First character has to be an opening brace
    // * First number is parsed after the first brace
    // * At the end of the first number, a comma must be present
    // * Second number is parsed after the comma
    // * At the end of the second number, a closing brace must be present
    float x = 0.f, y = 0.f;

    if (str.size() < 5) {
        return std::nullopt;
    }

    if (str[0] != '{') {
        return std::nullopt;
    }

    str.remove_prefix(1);
    auto parts = asp::iter::split(str, ',');
    auto firstStr = parts.next();
    auto secondStr = parts.next();

    if (!firstStr || !secondStr) {
        return std::nullopt;
    }

    auto result = geode::utils::numFromString<float>(*firstStr);
    if (!result) {
        return std::nullopt;
    }
    x = *result;

    // second num (has a brace at the end)

    if (!secondStr->ends_with('}')) {
        return std::nullopt;
    }
    secondStr->remove_suffix(1);
    result = geode::utils::numFromString<float>(*secondStr);
    if (!result) {
        return std::nullopt;
    }
    y = *result;

    return std::pair{x, y};
}

}
