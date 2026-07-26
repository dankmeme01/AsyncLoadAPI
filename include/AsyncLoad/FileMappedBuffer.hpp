#pragma once

#include <Geode/Result.hpp>
#include "config.hpp"
#include <span>
#include <stdint.h>
#include <stddef.h>

namespace AsyncLoad {

#ifdef _WIN32
using FileMappedBufferFd = void*;
static auto INVALID_FD = (void*)(uintptr_t)-1;
#else
using FileMappedBufferFd = int;
static int INVALID_FD = -1;
#endif

class AL_DLL FileMappedBuffer {
public:
    /// Expects file fd on posix or HANDLE casted to int on windows
    /// Takes ownership of the fd, will unmap and close it when the buffer is destroyed
    static geode::Result<FileMappedBuffer> createWithFd(FileMappedBufferFd fd);

#ifdef AsyncLoad_EXPORTS
    FileMappedBuffer(uint8_t* ptr, size_t size, FileMappedBufferFd fd) : m_ptr(ptr), m_size(size), m_fd(fd) {}
#endif

    FileMappedBuffer(const FileMappedBuffer&) = delete;
    FileMappedBuffer& operator=(const FileMappedBuffer&) = delete;
    FileMappedBuffer(FileMappedBuffer&&) noexcept;
    FileMappedBuffer& operator=(FileMappedBuffer&&) noexcept;

    ~FileMappedBuffer();

    operator std::span<const uint8_t>() const {
        return this->span();
    }

    std::span<const uint8_t> span() const {
        return std::span<const uint8_t>(this->data(), this->size());
    }

    uint8_t* data() const;
    size_t size() const;
    bool empty() const;

private:
    uint8_t* m_ptr = nullptr;
    size_t m_size;
    FileMappedBufferFd m_fd;

    FileMappedBuffer() = default;

    geode::Result<> _map();
    void _destroy();
};

}
