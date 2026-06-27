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

        for (int i = 1; i < 19; i++) {
            auto name = fmt::format("PlayerExplosion_{:02}.png", i);
            CCTextureCache::get()->removeTextureForKey(AsyncLoad::fullPathForFilename(name).c_str());

            auto spr = AsyncLoad::AsyncSprite::create(name);
            spr->setCallback([](auto r) {
                log::debug("AsyncSprite callback: {}", r);
            });
            spr->setScale(0.25f);
            spr->setPosition({300, 150});
            this->addChild(spr);
        }

        return true;
    }
};
