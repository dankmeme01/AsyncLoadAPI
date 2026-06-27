#pragma once
#include "Util.hpp"
#include "OpenGLIncludes.hpp"

namespace AsyncLoad {

class AL_DLL SmartPBO {
public:
    SmartPBO() = default;
    SmartPBO(GLuint val, size_t capacity);

    SmartPBO(SmartPBO&& other) noexcept;
    SmartPBO& operator=(SmartPBO&& other) noexcept;
    SmartPBO(const SmartPBO&) = delete;
    SmartPBO& operator=(const SmartPBO&) = delete;

    ~SmartPBO();

    GLuint get() const {
        return m_pbo;
    }

    size_t capacity() const {
        return m_capacity;
    }

    GLsync fence() const {
        return m_fence;
    }

    void destroy();

    /// Creates a synchronization fence, does nothing if unsupported or if a fence already exists.
    void createFence();
    /// Destroys a synchronization fence, does nothing if unsupported or if no fence exists.
    void destroyFence();
    /// Checks whether the PBO is currently busy (aka the fence has not been signaled), returns false if unsupported.
    bool isBusy() const;

    static SmartPBO create(size_t capacity);

private:
    GLuint m_pbo = 0;
    GLsync m_fence = 0;
    size_t m_capacity = 0;
};

}
