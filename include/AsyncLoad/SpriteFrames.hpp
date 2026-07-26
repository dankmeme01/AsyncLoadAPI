#pragma once

// Custom parsing for plist files containing cocos2d sprite frames.
// Does very minimal heap allocations, uses fast XML and float parsers.
// Proves to be ~8-9 times faster than the cocos2d implementation on Windows,
// and even faster on MacOS and iOS due to binary plists being used.

#include <Geode/Result.hpp>
#include <cocos2d.h>
#include <asp/iter.hpp>
#include "Util.hpp"

namespace AsyncLoad {

struct AL_DLL SpriteFrame {
    std::string name;
    cocos2d::CCPoint offset;
    cocos2d::CCSize sourceSize;
    cocos2d::CCRect textureRect;
    std::vector<std::string> aliases;
    bool textureRotated = false;
};

struct AL_DLL SpriteFrameMetadata {
    int format = -1;
    std::string textureFileName = "";
};

struct AL_DLL SpriteFrameData {
    struct Impl;
    SpriteFrameData(std::unique_ptr<Impl> impl);
    SpriteFrameData(const SpriteFrameData&) = delete;
    SpriteFrameData& operator=(const SpriteFrameData&) = delete;
    SpriteFrameData(SpriteFrameData&&);
    SpriteFrameData& operator=(SpriteFrameData&&);
    ~SpriteFrameData();

    const std::vector<SpriteFrame>& getFrames() const;
    std::vector<SpriteFrame>& getFrames();

    const SpriteFrameMetadata& getMetadata() const;

private:
    std::unique_ptr<Impl> m_impl;
};

/// Parses data from a .plist (binary on apple platforms, regular xml on others) file into a structure holding multiple sprite frames.
/// The structure can then be passed to `addSpriteFrames` to add them into `CCSpriteFrameCache`.
/// If `passBufferOwnership` is `true`, then you must not free the data buffer yourself, and it will be freed for you.
/// Otherwise, ensure that you keep the buffer for as long as the returned `SpriteFrameData` struct itself, because it may contain borrowed data.
/// This is fully thread-safe.
AL_DLL geode::Result<SpriteFrameData> parseSpriteFrames(void* data, size_t size, bool passBufferOwnership = false);

/// Adds sprite frames parsed from `parseSpriteFrames` into `CCSpriteFrameCache`.
/// This is not thread-safe.
AL_DLL void addSpriteFrames(const SpriteFrameData& frames, cocos2d::CCTexture2D* texture);

}
