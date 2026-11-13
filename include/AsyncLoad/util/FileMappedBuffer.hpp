#pragma once

#include <Geode/Result.hpp>
#include <asp/fs/File.hpp>
#include "config.hpp"
#include <span>
#include <stdint.h>
#include <stddef.h>

namespace AsyncLoad {

class AL_DLL FileMappedBuffer {
public:
    /// Takes ownership of the file and returns a buffer that is memory mapped to the entire contents.
    static geode::Result<FileMappedBuffer> createWithFile(asp::fs::File file);

    /// Like `createWithFile`, but reads a specific offset in file
    static geode::Result<FileMappedBuffer> createWithFileOffset(asp::fs::File file, size_t offset, size_t size);

#ifdef AsyncLoad_EXPORTS
    FileMappedBuffer(uint8_t* ptr, size_t size, asp::fs::File file) : m_ptr(ptr), m_size(size), m_file(std::move(file)) {}
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
    asp::fs::File m_file;
    uint8_t* m_ptr = nullptr;
    size_t m_size = 0;

    uint8_t* m_mapBase = nullptr;
    size_t m_mapLen = 0;

    FileMappedBuffer() = default;

    geode::Result<> _map(std::optional<size_t> offset, std::optional<size_t> size);
    void _destroy();
};

}
