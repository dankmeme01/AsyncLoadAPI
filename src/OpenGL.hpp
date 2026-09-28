#pragma once

#include <AsyncLoad/OpenGLIncludes.hpp>
#include <asp/iter.hpp>
#include <utility>

#ifndef GL_PIXEL_UNPACK_BUFFER
# define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
# define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_MAP_UNSYNCHRONIZED_BIT
# define GL_MAP_UNSYNCHRONIZED_BIT 0x0020
#endif
#ifndef GL_MAP_WRITE_BIT
# define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_RGBA8
# define GL_RGBA8 0x8058
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
# define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
# define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_TIMEOUT_EXPIRED
# define GL_TIMEOUT_EXPIRED 0x911B
#endif

using al_PFNGLTEXSTORAGE2D = void(*)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
using al_PFNGLMAPBUFFERRANGE = void*(*)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
using al_PFNGLFENCESYNC = GLsync(*)(GLenum condition, GLbitfield flags);
using al_PFNGLCLIENTWAITSYNC = GLenum(*)(GLsync sync, GLbitfield flags, GLuint64 timeout);
using al_PFNGLDELETESYNC = void(*)(GLsync GLsync);

inline struct OpenGLInfo {
    al_PFNGLTEXSTORAGE2D   pglTexStorage2D   = nullptr;
    al_PFNGLMAPBUFFERRANGE pglMapBufferRange = nullptr;
    al_PFNGLFENCESYNC      pglFenceSync      = nullptr;
    al_PFNGLCLIENTWAITSYNC pglClientWaitSync = nullptr;
    al_PFNGLDELETESYNC     pglDeleteSync     = nullptr;

    std::string_view versionStr;
    std::pair<int, int> version{0, 0};
    bool initialized = false;
    bool supportsPBO = false;
    bool supportsImmutableTex = false;
    bool supportsSync = false;

    void initialize();

private:
    void initOpenGLVersion();
    void initFeatures();
    static bool supportsGLExtension(std::string_view ext);
} g_opengl;

static void checkGL(std::string_view where) {
    GLenum err;
    while ((err = glGetError()) != GL_NO_ERROR) {
        geode::log::error("GL error at {}: 0x{:X}", where, err);
    }
}

static void checkGLDbg(std::string_view where) {
#ifdef AL_DEBUG
    checkGL(where);
#endif
}

static void clearGLError() {
    while (glGetError() != GL_NO_ERROR);
}
