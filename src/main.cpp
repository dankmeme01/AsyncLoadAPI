#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_game(Loaded) {
    log::info("{}", CCFileUtils::get()->getSearchPaths());
}