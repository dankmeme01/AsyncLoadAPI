#pragma once
#include "config.hpp"
#include "SpriteFrames.hpp"
#include "Images.hpp"

namespace AsyncLoad {

struct AL_DLL ImageLoadParams {
    using Callback = geode::Function<void(geode::Result<RawImage>)>;
    /// Never change this.
    uint32_t _structVersion = 1;

    /// The path to the image file to load asynchronously.
    /// If 'isFullPath' is 'true' (by default is 'false'), the path is assumed to be a full path.
    geode::ZStringView path;
    bool isFullPath = false;

    /// The byte vector to use for loading the image, in a format like PNG or WEBP (support depends on whether ImagePlus is installed).
    /// If this is not empty, then this is used over the path.
    std::vector<uint8_t> data;

    Callback callback;
};

struct AL_DLL TextureLoadParams {
    using Callback = geode::Function<void(
        geode::Result<geode::Ref<cocos2d::CCTexture2D>>
    )>;
    /// Never change this.
    uint32_t _structVersion = 1;

    /// The path to the image file to load asynchronously.
    /// If 'isFullPath' is 'true' (by default is 'false'), the path is assumed to be a full path.
    /// This will chain the image load operation followed by the texture load.
    geode::ZStringView path;
    bool isFullPath = false;

    /// The CCImage to use for the texture. If not null, then this is used over the path.
    cocos2d::CCImage* image = nullptr;

    /// The raw image to use for the texture. If not null, then this is used over the path and the CCImage.
    std::optional<RawImage> rawImage;

    Callback callback;
};

struct AL_DLL SpriteFramesLoadParams {
    using Callback = geode::Function<void(
        geode::Result<SpriteFrameData>
    )>;
    /// Never change this.
    uint32_t _structVersion = 1;

    /// The path to the plist file to load asynchronously.
    /// If 'isFullPath' is 'true' (by default is 'false'), the path is assumed to be a full path.
    geode::ZStringView path;
    bool isFullPath = false;

    /// The raw data of the plist file.
    /// On Apple systems, this is assumed to be a binary plist format. Everywhere else, it is XML data.
    std::vector<uint8_t> data;

    Callback callback;
};

struct [[nodiscard("call .leak() or store TaskHandle to not cancel it immediately")]] AL_DLL TaskHandle {
    TaskHandle(uint64_t id);
    TaskHandle(const TaskHandle&) = delete;
    TaskHandle& operator=(const TaskHandle&) = delete;
    TaskHandle(TaskHandle&&) noexcept;
    TaskHandle& operator=(TaskHandle&&) noexcept;
    ~TaskHandle();

    void leak();
    void cancel();

private:
    uint64_t m_id = 0;
};

class AL_DLL ALManager final {
public:
    static ALManager& get();

    ALManager(const ALManager&) = delete;
    ALManager& operator=(const ALManager&) = delete;
    ALManager(ALManager&&) = delete;
    ALManager& operator=(ALManager&&) = delete;

    ~ALManager();

    /// Begins to load an image in the background and invokes the given callback on main thread when done or errored.
    TaskHandle submitImageLoad(ImageLoadParams&& params);
    /// Begins to load a texture in the background and invokes the given callback on main thread when done or errored.
    TaskHandle submitTextureLoad(TextureLoadParams&& params);
    /// Begins to load sprite frames from a .plist file in the background and invokes the given callback on main thread when done or errored.
    /// This does not load the actual texture of the spritesheet, only the sprite frames. Use submitTextureLoad() for that.
    TaskHandle submitSpriteFramesLoad(SpriteFramesLoadParams&& params);

    void cancelTask(uint64_t id);

    void _retainPBO(uint32_t pbo);
    void _freePBOs();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    ALManager();
};

}
