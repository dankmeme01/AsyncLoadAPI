#pragma once
#include <Geode/c++stl/gdstdlib.hpp>

namespace AsyncLoad::string {

#ifdef GEODE_IS_ANDROID

inline std::string convert(const gd::string& str) {
    return std::string{str.data(), str.size()};
}

inline gd::string convert(const std::string& str) {
    return gd::string{str.data(), str.size()};
}

#else

inline std::string convert(std::string&& str) {
    return std::move(str);
}

inline std::string convert(const std::string& str) {
    return str;
}

#endif

}
