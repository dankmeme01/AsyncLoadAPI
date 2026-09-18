#pragma once
#include "util/config.hpp"
#include "util/SmartPBO.hpp"
#include "SpriteFrames.hpp"
#include "Images.hpp"
#include "Task.hpp"
#include "TaskGroup.hpp"

namespace AsyncLoad {

struct AL_DLL ImageLoadParams {
    using Callback = geode::Function<void(geode::Result<RawImage>)>;
    /// Never change this.
    uint32_t _structVersion = 1;

    /// The path to the image file to load asynchronously.
    /// If 'isFullPath' is 'true' (by default is 'false'), the path is assumed to be a full path.
    geode::ZStringView path;
    bool isFullPath = false;

    /// The byte chunk to use for loading the image, in a format like PNG or WEBP (support depends on whether ImagePlus is installed).
    /// If this is not empty, then this is used over the path.
    CachedBufferChunk data;

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
    CachedBufferChunk data;

    Callback callback;
};

class AL_DLL ALManager final {
public:
    struct Impl;

    static ALManager& get();

    ALManager(const ALManager&) = delete;
    ALManager& operator=(const ALManager&) = delete;
    ALManager(ALManager&&) = delete;
    ALManager& operator=(ALManager&&) = delete;

    ~ALManager();

    // High-level load APIs - high-level safe APIs that will use caches
    // Note that those may invoke the given callback *instantly* if cache is available.
    // In that case, they may return a blank TaskHandle.

    /// Creates a CCTexture2D from the image at the given path, invokes callback on main thread when done or errored.
    /// Uses CCTextureCache to skip loading if the texture is already loaded.
    /// Due to the use of cache, this function is NOT thread safe. Use lower-level alternatives for speed & thread-safety.
    ///
    /// NOTE: if the texture exists in cache, this will never invoke the callback immediately.
    /// There is a guarantee that the callback will be invoked to run as soon as possible (usually next frame, on main thread),
    /// and if that is not enough, you can use `loadTextureEager`.
    TaskHandle loadTexture(geode::ZStringView path, TextureLoadParams::Callback callback, bool fullPath = false);

    /// Like `loadTexture`, but if the texture is already in cache, the callback will be invoked immediately,
    /// and the returned handle will be empty.
    TaskHandle loadTextureEager(geode::ZStringView path, TextureLoadParams::Callback callback, bool fullPath = false);

    /// Loads a spritesheet file from the given path, invokes callback on main thread when done or errored.
    /// This loads the appropriate .png and .plist files in parallel and uses caches to avoid excessive loading.
    /// You must pass the name without any extension to this function.
    /// Due to the use of cache, this function is NOT thread safe. Use lower-level alternatives for speed & thread-safety.
    TaskGroup loadSpritesheet(std::string_view name, geode::Function<void(geode::Result<>)> callback);

    // Submission APIs - low-level APIs for high control.
    // They are fully thread-safe, and enqueue operations to happen in the background, giving you a handle to cancel it and letting you pass a callback.
    // They do not interact with any caches. For a slightly higher level API that will use cocos caches, see ALManager::loadXXXX APIs.

    /// Begins to load an image in the background and invokes the given callback on main thread when done or errored.
    TaskHandle submitImageLoad(ImageLoadParams&& params);
    /// Begins to load a texture in the background and invokes the given callback on main thread when done or errored.
    TaskHandle submitTextureLoad(TextureLoadParams&& params);
    /// Begins to load sprite frames from a .plist file in the background and invokes the given callback on main thread when done or errored.
    /// This does not load the actual texture of the spritesheet, only the sprite frames. Use submitTextureLoad() for that.
    TaskHandle submitSpriteFramesLoad(SpriteFramesLoadParams&& params);


    /// This function may be called at any time to push forward any pending asynchronous tasks that are waiting for the main thread.
    /// Note: this MUST only be called from the thread owning the OpenGL context, otherwise behavior is undefined.
    /// This is useful when you are blocking the main thread for a while to wait for resources to load,
    /// or if you are rewriting the game core loop and want to make it so the game actually does work instead of sleeping between frames.
    void lendMainThread();

    SmartPBO requestPBO(size_t capacity);
    void returnPBO(SmartPBO pbo);

private:
    std::unique_ptr<Impl> m_impl;

    ALManager();

    TaskHandle loadTextureInner(geode::ZStringView path, TextureLoadParams::Callback callback, bool fullPath, bool eager);
};

}
