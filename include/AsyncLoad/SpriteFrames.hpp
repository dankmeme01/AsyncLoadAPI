#pragma once

// Custom parsing for plist files containing cocos2d sprite frames.
// Does very minimal heap allocations, uses fast XML and float parsers.
// Proves to be ~8-9 times faster than the cocos2d implementation on Windows,
// and even faster on MacOS and iOS due to binary plists being used.

#include <Geode/Result.hpp>
#include <unordered_set>
#include <cocos2d.h>
#include <asp/iter.hpp>
#include "util/EventDefine.hpp"
#include "util/config.hpp"

namespace AsyncLoad {

struct SpriteFrame {
    std::string name;
    cocos2d::CCPoint offset;
    cocos2d::CCSize sourceSize;
    cocos2d::CCRect textureRect;
    std::vector<std::string> aliases;
    bool textureRotated = false;
};

struct SpriteFrameMetadata {
    int format = -1;
    std::string textureFileName;
};

struct SpriteFrameData {
    SpriteFrameMetadata metadata;
    std::vector<SpriteFrame> frames;
};

struct ParseSpriteFramesOptions {
    std::unordered_set<std::string> ignoreFrames;
    bool passBufferOwnership = false;
};

/// Parses data from a .plist (binary on apple platforms, regular xml on others) file into a structure holding multiple sprite frames.
/// The structure can then be passed to `addSpriteFrames` to add them into `CCSpriteFrameCache`.
/// If `passBufferOwnership` is `true`, then you must not free the data buffer yourself, and it will be freed for you.
/// This is fully thread-safe.
AL_EXPORT(
    geode::Result<SpriteFrameData>, parseSpriteFrames,
    (void* data, size_t size, const ParseSpriteFramesOptions& options = {}),
    (data, size, options)
);

/// Adds sprite frames parsed from `parseSpriteFrames` into `CCSpriteFrameCache`.
/// `key` is the key that will be used for sprite frame cache, typically it is the filename, aka `spritesheet.plist`
/// This is not thread-safe.
AL_EXPORT(void, addSpriteFrames, (const SpriteFrameData& frames, cocos2d::CCTexture2D* texture, std::string_view key), (frames, texture, key));
AL_EXPORT(void, addSpriteFrames, (const std::vector<SpriteFrame>& frames, cocos2d::CCTexture2D* texture, std::string_view key), (frames, texture, key));

}
