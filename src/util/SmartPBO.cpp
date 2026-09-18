#include <AsyncLoad/util/SmartPBO.hpp>
#include "OpenGL.hpp"

namespace AsyncLoad {

SmartPBO::SmartPBO(uint32_t val, size_t capacity) : m_pbo(val), m_capacity(capacity) {}

SmartPBO::SmartPBO(SmartPBO&& other) noexcept {
    *this = std::move(other);
}

SmartPBO& SmartPBO::operator=(SmartPBO&& other) noexcept {
    if (this != &other) {
        this->destroy();
        m_pbo = std::exchange(other.m_pbo, 0);
        m_fence = std::exchange(other.m_fence, nullptr);
        m_capacity = other.m_capacity;
    }
    return *this;
}

SmartPBO::~SmartPBO() {
    this->destroy();
}

SmartPBO SmartPBO::create(size_t capacity) {
#ifdef ENABLE_CACHE
    capacity = std::bit_ceil(capacity);
#endif

    uint32_t pbo;
    glGenBuffers(1, &pbo);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, capacity, nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    return SmartPBO(pbo, capacity);
}

void SmartPBO::createFence() {
    if (m_fence || !g_opengl.supportsSync) return;
    m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void SmartPBO::destroyFence() {
    if (!m_fence) return;
    glDeleteSync(m_fence);
    m_fence = nullptr;
}

bool SmartPBO::isBusy() const {
    if (!m_fence) return false;
    GLenum result = glClientWaitSync(m_fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    return result == GL_TIMEOUT_EXPIRED;
}

void SmartPBO::destroy() {
    if (m_pbo != 0) {
        glDeleteBuffers(1, &m_pbo);
        m_pbo = 0;
    }
    this->destroyFence();
}

}