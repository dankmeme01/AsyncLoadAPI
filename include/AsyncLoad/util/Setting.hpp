#pragma once
#include <Geode/loader/Mod.hpp>

namespace blaze {

template <geode::utils::string::ConstexprString S, typename T>
inline T const& getSettingFast() {
    static T value = (
        geode::listenForSettingChanges<T>(S.data(), [](T val) {
            value = std::move(val);
        }),
        geode::getMod()->getSettingValue<T>(S.data())
    );
    return value;
}

}
