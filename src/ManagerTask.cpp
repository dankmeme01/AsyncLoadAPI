#include "ManagerTask.hpp"
#include "OpenGL.hpp"
#include <AsyncLoad/Manager.hpp>
#include <Geode/utils/terminate.hpp>
#include <Geode/modify/CCTexture2D.hpp>
#include <AsyncLoad/assert.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <prevter.imageplus/include/events.hpp>

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

using namespace geode::prelude;

namespace AsyncLoad {

std::string_view format_as(TaskState state) {
    switch (state) {
        case TaskState::PreImageRead: return "PreImageRead";
        case TaskState::ImageRead: return "ImageRead";
        case TaskState::ImageReady: return "ImageReady";
        case TaskState::AsyncPboReady: return "AsyncPboReady";
        case TaskState::AsyncPboDone: return "AsyncPboDone";
        case TaskState::TextureReady: return "TextureReady";
        case TaskState::PrePlistRead: return "PrePlistRead";
        case TaskState::PlistRead: return "PlistRead";
        case TaskState::SpriteFramesReady: return "SpriteFramesReady";
        case TaskState::Failed: return "Failed";
        case TaskState::Invalid: return "Invalid";
    }
    std::unreachable();
}

std::string_view format_as(TaskGoal goal) {
    switch (goal) {
        case TaskGoal::Image: return "Image";
        case TaskGoal::Texture: return "Texture";
        case TaskGoal::SpriteFrames: return "SpriteFrames";
    }
    std::unreachable();
}

static void preparePath(auto& buf, std::string_view path, bool isFullPath) {
    if (isFullPath) {
        buf.append(path);
    } else {
        auto fp = fullPathForFilename(path);
        buf.append(fp);
    }
}

static bool shouldUsePBO() {
    static bool should = g_opengl.supportsPBO && Mod::get()->getSettingValue<bool>("use-pbos");
    return should;
}

static bool shouldUseAsyncPBO() {
    static bool should = shouldUsePBO() && Mod::get()->getSettingValue<bool>("use-async-pbo");
    return should;
}

ImageTask::ImageTask(ImageLoadParams&& params) : Task() {
    m_goal.store(TaskGoal::Image, std::memory_order::relaxed);
    m_callback = std::move(params.callback);

    if (!params.data.empty()) {
        // user provided raw image data, no need to read any files
        m_state.store(TaskState::ImageReady, std::memory_order::relaxed);
        m_imageData = std::move(params.data);
        return;
    }

    // user provided a path, prepare to read from it
    m_state.store(TaskState::PreImageRead, std::memory_order::relaxed);
    m_path = asp::BoxedString{params.path};
    m_pathIsFull = params.isFullPath;
}

bool ImageTask::finished() const {
    auto st = this->state();
    return st == TaskState::ImageReady || st == TaskState::Failed || this->cancelled();
}

void ImageTask::invokeCallback() {
    AL_DEBUG_ASSERT(this->finished());

    if (this->failed()) {
        if (m_callback) m_callback(Err(std::move(m_error)));
    } else {
        AL_DEBUG_ASSERT(m_image.has_value());
        if (m_callback) m_callback(Ok(std::move(*m_image)));
    }
}

TaskAdvanceResult ImageTask::advance(bool mainThread) {
    if (this->finished()) return TaskAdvanceResult::Finished;
    using enum TaskState;

    switch (this->state()) {
        case PreImageRead: {
            auto res = getFileData(m_path.c_str(), m_pathIsFull);
            if (!res) {
                this->fail(fmt::format("failed to read image file: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_imageData = std::move(*res);
            this->setState(ImageRead);
        } break;

        case ImageRead: {
            auto res = RawImage::create(m_imageData.span());
            if (!res) {
                this->fail(fmt::format("failed to decode image: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_image = std::move(*res);
            m_image->premultiply();
            this->setState(ImageReady);
            return TaskAdvanceResult::Finished;
        } break;

        default: {
            AL_ASSERT(false && "Invalid state for ImageTask");
        } break;
    }

    return TaskAdvanceResult::Pending;
}

TextureTask::TextureTask(TextureLoadParams&& params) : Task() {
    m_goal.store(TaskGoal::Texture, std::memory_order::relaxed);
    m_callback = std::move(params.callback);

    if (params.rawImage) {
        // user provided a raw image, no need to read any files
        m_state.store(TaskState::ImageReady, std::memory_order::relaxed);
        m_image = std::move(params.rawImage);
        return;
    } else if (params.image) {
        // user provided a CCImage, same deal
        m_state.store(TaskState::ImageReady, std::memory_order::relaxed);

        uint64_t w = params.image->m_nWidth;
        uint64_t h = params.image->m_nHeight;
        bool alpha = params.image->m_bHasAlpha;
        uint64_t byteSize = w * h * (3 + (uint64_t)alpha);

        auto res = RawImage::create(params.image);
        if (!res) {
            this->fail(fmt::format("failed to create RawImage from CCImage: {}", res.unwrapErr()));
            return;
        }

        m_image = std::move(*res);
        return;
    }

    // user provided a path, prepare to read from it
    m_state.store(TaskState::PreImageRead, std::memory_order::relaxed);
    m_path = asp::BoxedString{params.path};
    m_pathIsFull = params.isFullPath;
}

TextureTask::~TextureTask() {
    if (m_glTex != 0) {
        glDeleteTextures(1, &m_glTex);
    }

    if (m_glPbo) {
        ALManager::get().returnPBO(std::move(m_glPbo));
    }
}

bool TextureTask::finished() const {
    auto st = this->state();
    return st == TaskState::TextureReady || st == TaskState::Failed || this->cancelled();
}

void TextureTask::invokeCallback() {
    AL_DEBUG_ASSERT(this->finished());

    if (this->failed()) {
        if (m_callback) m_callback(Err(std::move(m_error)));
    } else {
        AL_DEBUG_ASSERT(m_texture != nullptr);
        if (m_callback) m_callback(Ok(std::move(m_texture)));
    }
}

TaskAdvanceResult TextureTask::advance(bool mainThread) {
    if (this->finished()) return TaskAdvanceResult::Finished;
    using enum TaskState;

    switch (this->state()) {
        case PreImageRead: {
            auto res = getFileData(m_path.c_str(), m_pathIsFull);
            if (!res) {
                this->fail(fmt::format("failed to read image file: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_imageData = std::move(*res);
            this->setState(ImageRead);
        } break;

        case ImageRead: {
            auto res = RawImage::create(m_imageData.span());
            if (!res) {
                this->fail(fmt::format("failed to decode image: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_image = std::move(*res);
            m_image->premultiply();
            this->setState(ImageReady);
            return TaskAdvanceResult::RequiresMainThread;
        } break;

        case ImageReady: {
            if (!mainThread) return TaskAdvanceResult::RequiresMainThread;

            g_opengl.initialize();

            if (shouldUseAsyncPBO()) {
                return this->startAsyncPBOLoad();
            } else if (shouldUsePBO()) {
                return this->startPBOLoad();
            } else {
                return this->startNoPBOLoad();
            }
        } break;

        case AsyncPboReady: {
            return this->doWriteIntoAsyncPBO();
        } break;

        case AsyncPboDone: {
            this->doFinalizeAsyncPBO();
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
    tex->m_bHasPremultipliedAlpha = m_image->hasAlpha;
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
    glBindTexture(GL_TEXTURE_2D, m_glTex);

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
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_glPbo.get());
}

TaskAdvanceResult TextureTask::startAsyncPBOLoad() {
    AL_BENCHMARK(this->preparePBO());
    int64_t byteSize = m_image->sizeBytes();
    auto flags = GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_UNSYNCHRONIZED_BIT;
    AL_BENCHMARK(m_mappedPboPtr = g_opengl.pglMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, byteSize, flags));
    checkGLDbg("glMapBufferRange");

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!m_mappedPboPtr) {
        utils::terminate(
            "PreloadManager: failed to map a PBO, likely ran out of memory! "
            "Please report this to the Globed developers and include the latest game log (not crashlog!)"
        );
    }

    this->setState(TaskState::AsyncPboReady);
    return TaskAdvanceResult::Pending;
}

TaskAdvanceResult TextureTask::doWriteIntoAsyncPBO() {
    AL_DEBUG_ASSERT(m_mappedPboPtr);

    auto size = m_image->sizeBytes();
    std::memcpy(m_mappedPboPtr, m_image->data.data(), size);

    this->setState(TaskState::AsyncPboDone);
    return TaskAdvanceResult::RequiresMainThread;
}

void TextureTask::doFinalizeAsyncPBO() {
    AL_BENCHMARK(clearGLError());
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_glPbo.get());
    GLboolean ok = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    AL_ASSERT(ok);

    glBindTexture(GL_TEXTURE_2D, m_glTex);

    int64_t w = m_image->width, h = m_image->height;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    AL_BENCHMARK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
    checkGL("glTexSubImage2D");

    // unbind texture & pbo
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // return the pbo
    ALManager::get().returnPBO(std::move(m_glPbo));

    m_texture = this->finalizeTexture(m_glTex);
    m_glTex = 0;

    this->setState(TaskState::TextureReady);
}

TaskAdvanceResult TextureTask::startPBOLoad() {
    AL_BENCHMARK(this->preparePBO());

    int64_t byteSize = m_image->sizeBytes();
    AL_BENCHMARK(glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, byteSize, m_image->data.data()));
    checkGLDbg("glBufferSubData");

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    AL_BENCHMARK(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_image->width, m_image->height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_texture = this->finalizeTexture(m_glTex);
    m_glTex = 0;

    // return the PBO to the manager so it can be reused later
    ALManager::get().returnPBO(std::move(m_glPbo));

    this->setState(TaskState::TextureReady);
    return TaskAdvanceResult::RequiresMainThread;
}

TaskAdvanceResult TextureTask::startNoPBOLoad() {
    GLuint num = 0;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &num);
    glBindTexture(GL_TEXTURE_2D, num);

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

    m_texture = this->finalizeTexture(num);
    this->setState(TaskState::TextureReady);

    return TaskAdvanceResult::Finished;
}

// Sprite frames task

SpriteFramesTask::SpriteFramesTask(SpriteFramesLoadParams&& params) : Task() {
    m_goal.store(TaskGoal::SpriteFrames, std::memory_order::relaxed);
    m_callback = std::move(params.callback);

    if (!params.data.empty()) {
        // user provided raw plist data, no need to read any files
        m_state.store(TaskState::PlistRead, std::memory_order::relaxed);
        m_data = std::move(params.data);
        return;
    }

    // user provided a path, prepare to read from it
    m_state.store(TaskState::PrePlistRead, std::memory_order::relaxed);
    m_path = asp::BoxedString{params.path};
    m_pathIsFull = params.isFullPath;
}

bool SpriteFramesTask::finished() const {
    auto st = this->state();
    return st == TaskState::SpriteFramesReady || st == TaskState::Failed || this->cancelled();
}

void SpriteFramesTask::invokeCallback() {
    AL_DEBUG_ASSERT(this->finished());

    if (this->failed()) {
        if (m_callback) m_callback(Err(std::move(m_error)));
    } else {
        AL_DEBUG_ASSERT(m_spriteFrames.has_value());
        if (m_callback) m_callback(Ok(std::move(*m_spriteFrames)));
    }
}

TaskAdvanceResult SpriteFramesTask::advance(bool mainThread) {
    if (this->finished()) return TaskAdvanceResult::Finished;
    using enum TaskState;

    switch (this->state()) {
        case PrePlistRead: {
            auto res = getFileData(m_path.c_str(), m_pathIsFull);
            if (!res) {
                this->fail(fmt::format("Failed to read plist file: {}", res.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            m_data = std::move(*res);
            this->setState(PlistRead);
        } break;

        case PlistRead: {
            auto result = parseSpriteFrames(m_data.data(), m_data.size(), false);
            if (!result) {
                this->fail(fmt::format("Failed to parse sprite frames (path: {}): {}", m_path, result.unwrapErr()));
                return TaskAdvanceResult::Finished;
            }
            this->setState(SpriteFramesReady);
        } break;

        default: {
            AL_ASSERT(false && "Invalid state for SpriteFramesTask");
        } break;
    }

    return TaskAdvanceResult::Pending;
}

}
