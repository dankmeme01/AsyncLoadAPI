#pragma once
#include <Geode/loader/Dispatch.hpp>
#include <Geode/utils/ZStringView.hpp>
#include "Manager.hpp"

#undef MY_MOD_ID
#define MY_MOD_ID "dankmeme.async-load-api"

namespace AsyncLoad {

/// Asynchronously loads the texture by the given path (must be a `.png` file) and puts it into `CCTextureCache`.
/// If the texture is already cached, immediately invokes the callback with the texture. If the mod is not loaded, returns an error.
/// Pass `fullPath = true` if the path is already a full path and you want to avoid search path resolution.
inline geode::Result<> loadTexture(
    geode::ZStringView path,
    TextureLoadParams::Callback callback,
    bool fullPath = false
) GEODE_EVENT_EXPORT(&loadTexture, (path, std::move(callback), fullPath));

/// Asynchronously loads the texture by the given path (must be a `.png` file) and puts it into `CCTextureCache`.
/// If the texture is already cached, succeeds and does nothing. If the mod is not loaded, returns an error.
/// If the texture fails to load, the error is swallowed. To be able to handle errors or know when the texture finishes loading,
/// use `loadTexture` instead.
///
/// This function is a very high level, one-shot API for mods that want to preload certain resources
/// on game launch, to avoid lagspikes when using `CCSprite::create` some time later.
inline geode::Result<> preload(geode::ZStringView path) GEODE_EVENT_EXPORT(&preload, (path));

}
