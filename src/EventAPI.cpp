#define GEODE_DEFINE_EVENT_EXPORTS
#include <AsyncLoad/EventAPI.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

Result<> loadTexture(
    ZStringView path,
    TextureLoadParams::Callback callback,
    bool fullPath
) {
    ALManager::get().loadTexture(path, std::move(callback), fullPath).leak();
    return Ok();
}

Result<> preload(ZStringView path) {
    ALManager::get().loadTexture(path, [](auto) {}).leak();
    return Ok();
}

}
