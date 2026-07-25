#include <Geode/Geode.hpp>
#include <AsyncLoad/nodes/AsyncSprite.hpp>
#include <AsyncLoad/FileUtils.hpp>

using namespace geode::prelude;

$on_game(Loaded) {
    log::info("{}", CCFileUtils::get()->getSearchPaths());
}

#include <Geode/modify/MenuLayer.hpp>

class $modify(MenuLayer) {
    bool init() {
        MenuLayer::init();


        return true;
    }
};
