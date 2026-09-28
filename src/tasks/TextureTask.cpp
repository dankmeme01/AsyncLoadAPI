#include "TextureTask.hpp"
#include <AsyncLoad/FileUtils.hpp>
#include <Geode/utils/terminate.hpp>
#include <OpenGL.hpp>

using namespace geode::prelude;

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

static bool shouldUsePBO() {
    static bool should = g_opengl.supportsPBO && Mod::get()->getSettingValue<bool>("use-pbos");
    return should;
}

static bool shouldUseAsyncPBO() {
    static bool should = shouldUsePBO() && Mod::get()->getSettingValue<bool>("use-async-pbo");
    return should;
}

namespace AsyncLoad {
TextureTask::TextureTask(TextureLoadParams&& params, std::shared_ptr<Control> ctl) : TypedTask(std::move(ctl)) {
    if (params.rawImage) {
        // user provided a raw image, no need to read any files
        m_state = State::ImageDecoded;
        m_image = std::move(params.rawImage);
        return;
    } else if (params.image) {
        // user provided a CCImage, same deal
        m_state = State::ImageDecoded;

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


TaskAdvanceResult TextureTask::advance(bool mainThread) {
    if (!this->shouldRun()) return TaskAdvanceResult::Finished;

    switch (m_state) {
        case State::PreImageRead: {
            auto in = asp::Instant::now();

            // use memory mapping whenever possible
            if (canMapFile(m_path.c_str())) {
                auto res = getMappedFile(m_path.c_str(), m_pathIsFull);
                if (!res) {
                    this->fail(fmt::format("failed to read image file (mapped mode): {}", res.unwrapErr()));
                    return TaskAdvanceResult::Finished;
                }
                m_imageData = std::move(*res);
            } else {
                auto res = getFileData(m_path.c_str(), m_pathIsFull);
                if (!res) {
                    this->fail(fmt::format("failed to read image file: {}", res.unwrapErr()));
                    return TaskAdvanceResult::Finished;
                }
                m_imageData = std::move(*res);
            }

            AL_TRACE("Image read finished in {}", in.elapsed());
            m_state = State::ImageRead;
        } break;

        case State::ImageRead: {
            std::span<const uint8_t> data;
            if (std::holds_alternative<CachedBufferChunk>(m_imageData)) {
                data = std::get<CachedBufferChunk>(m_imageData).span();
            } else if (std::holds_alternative<FileMappedBuffer>(m_imageData)) {
                data = std::get<FileMappedBuffer>(m_imageData).span();
            }

            auto res = RawImage::create(data);
            if (!res) {
                this->fail(fmt::format("failed to decode image: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_imageData = CachedBufferChunk{}; // release buffer
            m_image = std::move(*res);
            m_image->premultiply();
            m_state = State::ImageReady;
            return TaskAdvanceResult::RequiresMainThread;
        } break;

        case State::ImageDecoded: {
            AL_DEBUG_ASSERT(m_image && "m_image must be set in ImageDecoded state");
            m_image->premultiply();
            m_state = State::ImageReady;
            return TaskAdvanceResult::RequiresMainThread;
        } break;

        case State::ImageReady: {
            if (!mainThread) return TaskAdvanceResult::RequiresMainThread;
            AL_DEBUG_ASSERT(m_image && m_image->hasAlpha && m_image->premultiplied && "m_image must have premultiplied alpha in ImageReady state");

            g_opengl.initialize();

            if (shouldUseAsyncPBO()) {
                return this->startAsyncPBOLoad();
            } else if (shouldUsePBO()) {
                return this->startPBOLoad();
            } else {
                return this->startNoPBOLoad();
            }
        } break;

        case State::AsyncPboReady: {
            return this->doWriteIntoAsyncPBO();
        } break;

        case State::AsyncPboDone: {
            return this->doFinalizeAsyncPBO();
        } break;

        default: {
            AL_ASSERT(false && "Invalid state for TextureTask");
        } break;
    }

    return TaskAdvanceResult::Pending;
}


Ref<CCTexture2D> TextureTask::finalizeTexture(GLuint num) {
    auto tex = Ref<CCTexture2D>::adopt(new CCTexture2D());
    tex->m_uName = num;
    tex->m_tContentSize = CCSize {
        (float)m_image->width,
        (float)m_image->height,
    };
    tex->m_uPixelsWide = m_image->width;
    tex->m_uPixelsHigh = m_image->height;
    tex->m_ePixelFormat = m_image->hasAlpha ? kCCTexture2DPixelFormat_RGBA8888 : kCCTexture2DPixelFormat_RGB888;
    tex->m_fMaxS = 1.f;
    tex->m_fMaxT = 1.f;
    tex->m_bHasPremultipliedAlpha = m_image->premultiplied;
    tex->m_bHasMipmaps = false;

    tex->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTexture));
    return tex;
}

void TextureTask::preparePBO() {
    AL_ASSERT(m_image->hasAlpha);
    int64_t width = m_image->width;
    int64_t height = m_image->height;
    int64_t byteSize = m_image->sizeBytes();

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
    AL_TRACE("allocated PBO {} with capacity {} for {}", m_glPbo.get(), m_glPbo.capacity(), this->name());
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_glPbo.get());
}

TaskAdvanceResult TextureTask::startAsyncPBOLoad() {
    AL_BENCHMARK(this->preparePBO());
    int64_t byteSize = m_image->sizeBytes();
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
    m_state = State::AsyncPboReady;

    return TaskAdvanceResult::Pending;
}

TaskAdvanceResult TextureTask::doWriteIntoAsyncPBO() {
    AL_DEBUG_ASSERT(m_mappedPboPtr);

    auto size = m_image->sizeBytes();
    std::memcpy(m_mappedPboPtr, m_image->data.data(), size);

    m_state = State::AsyncPboDone;
    return TaskAdvanceResult::RequiresMainThread;
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

TaskAdvanceResult TextureTask::doFinalizeAsyncPBO() {
    AL_BENCHMARK(clearGLError());
    this->unmapPBO(false);

    ccGLBindTexture2D(m_glTex);

    int64_t w = m_image->width, h = m_image->height;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    AL_BENCHMARK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    checkGL("glTexSubImage2D");

    // unbind texture & pbo
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    ccGLBindTexture2D(0);

    // return the pbo
    ALManager::get().returnPBO(std::move(m_glPbo));

    auto texture = this->finalizeTexture(m_glTex);
    m_glTex = 0;

    this->succeed(std::move(texture));
    return TaskAdvanceResult::Finished;
}

TaskAdvanceResult TextureTask::startPBOLoad() {
    AL_BENCHMARK(this->preparePBO());

    int64_t byteSize = m_image->sizeBytes();
    AL_BENCHMARK(glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, byteSize, m_image->data.data()));
    checkGLDbg("glBufferSubData");

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    AL_BENCHMARK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_image->width, m_image->height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    ccGLBindTexture2D(0);

    auto texture = this->finalizeTexture(m_glTex);
    m_glTex = 0;

    // return the PBO to the manager so it can be reused later
    ALManager::get().returnPBO(std::move(m_glPbo));

    this->succeed(std::move(texture));
    return TaskAdvanceResult::Finished;
}

TaskAdvanceResult TextureTask::startNoPBOLoad() {
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

    this->succeed(this->finalizeTexture(num));

    return TaskAdvanceResult::Finished;
}
void TextureTask::succeed(Ref<CCTexture2D> texture) {
    AL_ASSERT(texture != nullptr);

    m_state = State::TextureReady;
    this->complete(Ok(std::move(texture)));
}

void TextureTask::fail(std::string message) {
    m_state = State::Failed;
    this->complete(Err(std::move(message)));
}


}
