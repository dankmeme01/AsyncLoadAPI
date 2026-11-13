#pragma once
#include "TaskImpl.hpp"
#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <AsyncLoad/util/FileMappedBuffer.hpp>
#include <util/PngDecoder.hpp>

namespace AsyncLoad {

struct TextureTask final : CrtpTask<TextureTask, geode::Ref<cocos2d::CCTexture2D>> {
    enum class State : uint8_t {
        /// We have the path and we are about to read the file from the disk.
        PreImageRead,
        /// We have the data of the image file. This step splits later into 2 paths: streaming and one-shot decode.
        ImageRead,
        /// Waiting for a memory budget grant
        RawImageBudgetWait,


        //////////////// == Streaming ==


        /// Stream decoder is ready
        StreamDecoderReady,
        /// Stream decoder has finished
        StreamDecoderDone,


        //////////////// == RawImage one-shot decode ==


        /// We have the image as RGB/RGBA pixels, but it may not yet be premultiplied.
        RawImageReady,
        /// Image has been fully decoded and a raw image is available. A texture may now be created with this.
        ImageReady,


        //////////////// == Shared (joining two paths) ==


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
    std::optional<FileMetadata> m_fileMetadata;
    std::variant<CachedBufferChunk, FileMappedBuffer> m_imageData; // bytes in an arbitrary image format
    std::optional<RawImage> m_image;
    std::optional<PendingBudgetGrant> m_memoryBudgetGrant;
    std::optional<PngDecoder> m_decoder;
    GLuint m_glTex = 0;
    SmartPBO m_glPbo;
    void* m_mappedPboPtr = nullptr;
    bool m_pathIsFull;
    bool m_pboMapped = false;
    bool m_joinable = false;

    TextureTask(TextureLoadParams&& params, bool joinable, std::shared_ptr<Control> ctl);
    ~TextureTask();

    // bool finished() const override;
    // void invokeCallback() override;
    TaskAdvanceResult advance(bool mainThread = false) override;

    void fail(std::string message);
    void succeed(geode::Ref<cocos2d::CCTexture2D> texture);

    void preparePBO();
    void preparePBO(int64_t width, int64_t height);
    void mapPBO(size_t byteSize);
    void unmapPBO(bool unbind = false);

    void startAsyncPBOLoad();
    void doWriteIntoAsyncPBO();
    void doFinalizeAsyncPBO();
    void startStreamingPBOLoad();
    void doFinalizeStreamingPBO();
    void doFinalizePBO(int64_t width, int64_t height);

    void startNoPBOLoad();

    geode::Ref<cocos2d::CCTexture2D> finalizeTexture(GLuint tex, uint32_t width, uint32_t height);

    std::span<const uint8_t> getImageData() const;

    void freeResources();
};

}
