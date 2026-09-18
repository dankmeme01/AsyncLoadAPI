#pragma once
#include "TaskImpl.hpp"
#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/util/FileMappedBuffer.hpp>

namespace AsyncLoad {

struct TextureTask final : TypedTask<geode::Ref<cocos2d::CCTexture2D>> {
    enum class State : uint8_t {
        /// We have the path and we are about to read the file from the disk.
        PreImageRead,
        /// We have the data of the image file, and we are about to decode it from a format like PNG or WEBP into raw pixels.
        ImageRead,
        /// Image has been fully decoded and a raw image is available. A texture may now be created with this.
        ImageReady,

        /// A PBO has been mapped and we are ready to write into it from a thread
        AsyncPboReady,
        /// Image has either been decoded into a PBO or data has been memcpied from CCImage to the PBO.
        /// The PBO is now ready to be finalized into a CCTexture2D.
        AsyncPboDone,
        /// A CCTexture2D is now ready and available.
        TextureReady,

        Failed,
    };

    State m_state;
    asp::BoxedString m_path;
    std::variant<CachedBufferChunk, FileMappedBuffer> m_imageData; // bytes in an arbitrary image format
    std::optional<RawImage> m_image;
    GLuint m_glTex = 0;
    SmartPBO m_glPbo;
    void* m_mappedPboPtr = nullptr;
    bool m_pathIsFull;

    TextureTask(TextureLoadParams&& params, std::shared_ptr<Control> ctl);
    ~TextureTask();

    // bool finished() const override;
    // void invokeCallback() override;
    TaskAdvanceResult advance(bool mainThread = false) override;

    void fail(std::string message);
    void succeed(geode::Ref<cocos2d::CCTexture2D> texture);


    void preparePBO();
    TaskAdvanceResult startAsyncPBOLoad();
    TaskAdvanceResult doWriteIntoAsyncPBO();
    TaskAdvanceResult startPBOLoad();
    TaskAdvanceResult startNoPBOLoad();
    TaskAdvanceResult doFinalizeAsyncPBO();

    geode::Ref<cocos2d::CCTexture2D> finalizeTexture(GLuint tex);

    std::string_view typeName() const override {
        return "TextureTask";
    }
};

}
