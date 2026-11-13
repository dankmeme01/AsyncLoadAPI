#include "TextureTask.hpp"
#include <AsyncLoad/FileUtils.hpp>
#include <Geode/utils/terminate.hpp>
#include <util/Setting.hpp>
#include <OpenGL.hpp>

using namespace geode::prelude;

static constexpr size_t STREAMING_DECODE_BUFFER_SIZE = 512 * 1024;

#ifdef AL_DEBUG
# define AL_BENCHMARK(code) \
    do { \
        auto start = asp::Instant::now(); \
        code; \
        auto taken = start.elapsed(); \
        if (taken.micros() > 500) AL_TRACE("BENCHMARK: {} took {}", #code, taken); \
    } while (0)
#else
# define AL_BENCHMARK(code) \
    do { \
        code; \
    } while (0)
#endif


namespace AsyncLoad {

static bool shouldUsePBO() {
    static bool should = g_opengl.supportsPBO && getSettingFast<"use-pbos", bool>();
    return should;
}

static bool shouldUseStreamingDecode() {
    static bool should = shouldUsePBO() && getSettingFast<"use-streaming-decode", bool>();
    return should;
}

TextureTask::TextureTask(TextureLoadParams&& params, bool joinable, std::shared_ptr<Control> ctl) : CrtpTask(std::move(ctl)) {
    m_joinable = joinable;

    if (params.rawImage) {
        // user provided a raw image, no need to read any files
        m_state = State::RawImageReady;
        m_image = std::move(params.rawImage);
        return;
    } else if (params.image) {
        // user provided a CCImage, same deal
        m_state = State::RawImageReady;

        auto res = RawImage::create(params.image);
        if (!res) {
            this->fail(fmt::format("failed to create RawImage from CCImage: {}", res.unwrapErr()));
            return;
        }

        m_image = std::move(*res);
        return;
    }

    // user provided a path, prepare to read from it
    m_state = State::PreImageRead;
    m_path = asp::BoxedString{params.path};
    m_pathIsFull = params.isFullPath;
}

TextureTask::~TextureTask() {
    if (m_glTex != 0) {
        ccGLDeleteTexture(m_glTex);
    }

    if (m_glPbo) {
        this->unmapPBO(true);
        ALManager::get().returnPBO(std::move(m_glPbo));
    }
}

static std::string maybeFullPath(std::string_view p, bool isFull) {
    if (isFull) {
        return std::string{p};
    } else {
        return fullPathForFilename(p);
    }
}

TaskAdvanceResult TextureTask::advance(bool mainThread) {
    if (!this->shouldRun()) return TaskAdvanceResult::Finished;

    switch (m_state) {
        case State::PreImageRead: {
            auto in = asp::Instant::now();

            auto path = maybeFullPath(m_path, m_pathIsFull);
            if (path.empty()) {
                this->fail(fmt::format("could not find full path for '{}'", m_path));
                return TaskAdvanceResult::Finished;
            }

            auto res = getFileMetadata(path, true);
            if (!res) {
                this->fail(fmt::format("failed to get file metadata: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }

            m_fileMetadata = std::move(*res);

            // use memory mapping whenever possible
            if (m_fileMetadata->canMap) {
                auto res = getMappedFile(path, true);
                if (!res) {
                    this->fail(fmt::format("failed to read image file (mapped mode): {}", res.unwrapErr()));
                    return TaskAdvanceResult::Finished;
                }
                m_imageData = std::move(*res);
            } else {
                auto res = getFileData(path, true);
                if (!res) {
                    this->fail(fmt::format("failed to read image file: {}", res.unwrapErr()));
                    return TaskAdvanceResult::Finished;
                }
                m_imageData = std::move(*res);
            }

            AL_TRACE_NOISY("Image read finished in {} (mmap: {}, size: {})", in.elapsed(), m_fileMetadata->canMap, m_fileMetadata->size);
            m_state = State::ImageRead;
        } break;

        case State::ImageRead: {
            auto data = this->getImageData();

            if (shouldUseStreamingDecode()) {
                auto res = PngDecoder::create(data, PngDecodeMode::Streaming);
                if (res) {
                    m_decoder = std::move(*res);
                    m_state = State::StreamDecoderReady;
                    return TaskAdvanceResult::RequiresMainThread;
                }

                AL_TRACE_NOISY("PngDecoder::create failed for '{}': {}", m_path, res.unwrapErr());
            }

            m_state = State::RawImageBudgetWait;

            // request a budget grant for the memory we will use
            size_t decodedSizeGuess = data.size() * 8;
            if (auto pngSize = RawImage::getDecodedSize(data)) {
                decodedSizeGuess = *pngSize;
            }

            if (!m_memoryBudgetGrant) {
                m_memoryBudgetGrant = ALManager::get().requestMemoryBudgetOrWait(decodedSizeGuess, this->handle());
            }

            // suspend if we did not get the grant yet
            if (!m_memoryBudgetGrant->fulfilled()) {
                log::debug("not enough memory to load '{}' (want {} bytes), suspending task", m_path, decodedSizeGuess);
                return TaskAdvanceResult::Suspend;
            }

            return TaskAdvanceResult::Pending;
        } break;

        /// Streaming

        case State::StreamDecoderReady: {
            AL_DEBUG_ASSERT(m_decoder);

            if (mainThread) {
                this->startStreamingPBOLoad();
                // return to the same state but on worker thread instead of main thread
                return TaskAdvanceResult::Pending;
            }

            size_t imageSize = m_decoder->totalSize();
            size_t height = m_decoder->height();
            size_t rowByteSize = m_decoder->width() * 4;
            // sanity check
            AL_DEBUG_ASSERT(imageSize == rowByteSize * m_decoder->height());

            auto buffer = BufferCache::get().get(std::max(STREAMING_DECODE_BUFFER_SIZE, rowByteSize));
            size_t rowsPerChunk = buffer.capacity() / rowByteSize;

            for (size_t row = 0; row < height; row += rowsPerChunk) {
                size_t rows = std::min(rowsPerChunk, height - row);
                for (size_t i = 0; i < rows; i++) {
                    auto res = m_decoder->decodeRow(buffer.data() + i * rowByteSize, rowByteSize);
                    if (!res) {
                        this->fail(fmt::format("failed to decode row {}: {}", i, res.unwrapErr()));
                        return TaskAdvanceResult::Finished;
                    }

                }

                // flush this chunk out into the pbo
                size_t offset = row * rowByteSize;
                size_t size = rows * rowByteSize;

                // premultiply
                premultiplyAlphaInplace(buffer.data(), size);

                std::memcpy((uint8_t*)m_mappedPboPtr + offset, buffer.data(), size);
            }

            m_state = State::StreamDecoderDone;
            return TaskAdvanceResult::RequiresMainThread;
        } break;

        case State::StreamDecoderDone: {
            this->doFinalizeStreamingPBO();
            m_state = State::TextureReady;
            return TaskAdvanceResult::Finished;
        } break;


        /// RawImage decoding


        case State::RawImageBudgetWait: {
            auto data = this->getImageData();

            auto res = RawImage::create(data);
            if (!res) {
                this->fail(fmt::format("failed to decode image: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_image = std::move(*res);
            m_state = State::RawImageReady;
            return TaskAdvanceResult::Pending;
        } break;

        case State::RawImageReady: {
            AL_DEBUG_ASSERT(m_image && "m_image must be set in ImageDecoded state");
            m_image->premultiply();
            m_state = State::ImageReady;
            return TaskAdvanceResult::RequiresMainThread;
        } break;

        case State::ImageReady: {
            if (!mainThread) return TaskAdvanceResult::RequiresMainThread;
            AL_DEBUG_ASSERT(m_image && m_image->hasAlpha && m_image->premultiplied && "m_image must have premultiplied alpha in ImageReady state");

            m_imageData = CachedBufferChunk{}; // release buffer

            g_opengl.initialize();

            if (shouldUsePBO()) {
                this->startAsyncPBOLoad();
                m_state = State::AsyncPboReady;
                return TaskAdvanceResult::Pending;
            } else {
                this->startNoPBOLoad();
                return TaskAdvanceResult::Finished;
            }
        } break;

        case State::AsyncPboReady: {
            this->doWriteIntoAsyncPBO();
            return TaskAdvanceResult::RequiresMainThread;
        } break;

        case State::AsyncPboDone: {
            this->doFinalizeAsyncPBO();
            return TaskAdvanceResult::Finished;
        } break;

        default: {
            AL_ASSERT(false && "Invalid state for TextureTask");
        } break;
    }

    return TaskAdvanceResult::Pending;
}


Ref<CCTexture2D> TextureTask::finalizeTexture(GLuint num, uint32_t width, uint32_t height) {
    auto tex = Ref<CCTexture2D>::adopt(new CCTexture2D());
    tex->m_uName = num;
    tex->m_tContentSize = CCSize {
        (float)width,
        (float)height,
    };
    tex->m_uPixelsWide = width;
    tex->m_uPixelsHigh = height;
    tex->m_ePixelFormat = kCCTexture2DPixelFormat_RGBA8888;
    tex->m_fMaxS = 1.f;
    tex->m_fMaxT = 1.f;
    tex->m_bHasPremultipliedAlpha = true;
    tex->m_bHasMipmaps = false;

    tex->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTexture));
    return tex;
}

void TextureTask::preparePBO() {
    AL_ASSERT(m_image && m_image->hasAlpha);
    return this->preparePBO(m_image->width, m_image->height);
}

void TextureTask::preparePBO(int64_t width, int64_t height) {
    int64_t byteSize = width * height * 4;

    // some cocos code leaves an error for us
    clearGLError();

    glGenTextures(1, &m_glTex);
    ccGLBindTexture2D(m_glTex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (g_opengl.supportsImmutableTex) {
        g_opengl.pglTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
        checkGLDbg("glTexStorage2D");
    } else {
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            width,
            height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr
        );
        checkGLDbg("glTexImage2D");
    }

    m_glPbo = ALManager::get().requestPBO(byteSize);
    AL_TRACE_NOISY("allocated PBO {} with capacity {} for {}", m_glPbo.get(), m_glPbo.capacity(), this->name());
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_glPbo.get());
}

void TextureTask::mapPBO(size_t byteSize) {
    auto flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
    AL_BENCHMARK(m_mappedPboPtr = g_opengl.pglMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, byteSize, flags));
    checkGLDbg("glMapBufferRange");

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    ccGLBindTexture2D(0);

    if (!m_mappedPboPtr) {
        utils::terminate(
            "failed to map a PBO, likely ran out of memory! "
            "Please report this to the developers and include the latest game log (not crashlog!)"
        );
    }

    m_pboMapped = true;
}

void TextureTask::unmapPBO(bool unbind) {
    if (!m_glPbo) return;

    if (m_pboMapped) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_glPbo.get());
        GLboolean ok = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        AL_ASSERT(ok);

        m_pboMapped = false;
    }

    if (unbind) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }
}

void TextureTask::startAsyncPBOLoad() {
    AL_BENCHMARK(this->preparePBO());
    this->mapPBO(m_image->sizeBytes());
}

void TextureTask::startStreamingPBOLoad() {
    AL_DEBUG_ASSERT(m_decoder);

    AL_BENCHMARK(this->preparePBO(m_decoder->width(), m_decoder->height()));
    this->mapPBO(m_decoder->totalSize());
}

void TextureTask::doWriteIntoAsyncPBO() {
    AL_DEBUG_ASSERT(m_mappedPboPtr);

    auto size = m_image->sizeBytes();
    std::memcpy(m_mappedPboPtr, m_image->data.data(), size);

    m_state = State::AsyncPboDone;
}

void TextureTask::doFinalizeAsyncPBO() {
    AL_BENCHMARK(clearGLError());
    this->unmapPBO(false);

    ccGLBindTexture2D(m_glTex);

    this->doFinalizePBO(m_image->width, m_image->height);
}

void TextureTask::doFinalizeStreamingPBO() {
    AL_BENCHMARK(clearGLError());
    this->unmapPBO(false);

    ccGLBindTexture2D(m_glTex);

    this->doFinalizePBO(m_decoder->width(), m_decoder->height());

    m_decoder.reset();
}

void TextureTask::doFinalizePBO(int64_t width, int64_t height) {
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    AL_BENCHMARK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    checkGL("glTexSubImage2D");

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    ccGLBindTexture2D(0);

    auto texture = this->finalizeTexture(m_glTex, width, height);
    m_glTex = 0;

    // return the PBO to the manager so it can be reused later
    ALManager::get().returnPBO(std::move(m_glPbo));

    this->succeed(std::move(texture));
}

void TextureTask::startNoPBOLoad() {
    GLuint num = 0;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &num);
    ccGLBindTexture2D(num);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    AL_BENCHMARK(glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        m_image->width,
        m_image->height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        m_image->data.data()
    ));

    // unbind texture
    ccGLBindTexture2D(0);

    this->succeed(this->finalizeTexture(num, m_image->width, m_image->height));
}

void TextureTask::succeed(Ref<CCTexture2D> texture) {
    AL_ASSERT(texture != nullptr);

    m_state = State::TextureReady;
    this->complete(Ok(std::move(texture)));

    // free everything
    this->freeResources();
}

void TextureTask::fail(std::string message) {
    m_state = State::Failed;
    this->complete(Err(std::move(message)));

    // free everything
    this->freeResources();
}

void TextureTask::freeResources() {
    m_imageData = CachedBufferChunk{};
    m_image.reset();
    m_memoryBudgetGrant.reset();
    m_decoder.reset();
}

std::span<const uint8_t> TextureTask::getImageData() const {
    if (std::holds_alternative<CachedBufferChunk>(m_imageData)) {
        return std::get<CachedBufferChunk>(m_imageData).span();
    } else if (std::holds_alternative<FileMappedBuffer>(m_imageData)) {
        return std::get<FileMappedBuffer>(m_imageData).span();
    } else {
        AL_ASSERT(false);
    }
}

}
