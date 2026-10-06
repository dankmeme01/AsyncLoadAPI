#include <AsyncLoad/SpriteFrames.hpp>
#include <AsyncLoad/FileUtils.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

void addSpriteFrames(const SpriteFrameData& frames, cocos2d::CCTexture2D* texture, std::string_view key) {
    auto& fs = frames.getFrames();

    auto sfcache = CCSpriteFrameCache::get();

    for (const auto& frame : fs) {
        if (sfcache->m_pSpriteFrames->objectForKey(frame.name)) {
            continue;
        }

        // create sprite frame
        auto spriteFrame = Ref<CCSpriteFrame>::adopt(new CCSpriteFrame());
        bool result = spriteFrame->initWithTexture(
            texture,
            frame.textureRect,
            frame.textureRotated,
            frame.offset,
            frame.sourceSize
        );

        if (!result) {
            log::warn("Failed to initialize sprite frame for {}", frame.name);
            continue;
        }

        // add sprite frame
        sfcache->m_pSpriteFrames->setObject(spriteFrame, frame.name);

        // if there are any aliases, add them as well
        if (!frame.aliases.empty()) {
            // create one CCString and reuse it
            auto fnamestr = CCString::create(frame.name);

            for (const auto& alias : frame.aliases) {
                sfcache->m_pSpriteFramesAliases->setObject(
                    fnamestr,
                    alias
                );
            }
        }
    }

    sfcache->m_pLoadedFileNames->insert(gd::string{key.data(), key.size()});
    // for good measure, insert full path too
    sfcache->m_pLoadedFileNames->insert(fullPathForFilename(key));
}

}
