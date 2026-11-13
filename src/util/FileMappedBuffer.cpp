#include <AsyncLoad/util/FileMappedBuffer.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

Result<FileMappedBuffer> FileMappedBuffer::createWithFile(asp::fs::File file) {
    FileMappedBuffer buf;
    buf.m_file = std::move(file);
    auto res = buf._map(std::nullopt, std::nullopt);
    if (!res) return Err(std::move(res).unwrapErr());
    return Ok(std::move(buf));
}

Result<FileMappedBuffer> FileMappedBuffer::createWithFileOffset(asp::fs::File file, size_t offset, size_t size) {
    FileMappedBuffer buf;
    buf.m_file = std::move(file);
    auto res = buf._map(offset, size);
    if (!res) return Err(std::move(res).unwrapErr());
    return Ok(std::move(buf));
}

FileMappedBuffer::~FileMappedBuffer() {
    this->_destroy();
}

FileMappedBuffer::FileMappedBuffer(FileMappedBuffer&& other) noexcept {
    *this = std::move(other);
}

FileMappedBuffer& FileMappedBuffer::operator=(FileMappedBuffer&& other) noexcept {
    if (this != &other) {
        this->_destroy();
        m_file = std::move(other.m_file);
        m_ptr = std::exchange(other.m_ptr, nullptr);
        m_size = std::exchange(other.m_size, 0);
        m_mapBase = std::exchange(other.m_mapBase, nullptr);
        m_mapLen = std::exchange(other.m_mapLen, 0);
    }
    return *this;
}

uint8_t* FileMappedBuffer::data() const {
    return m_ptr;
}

size_t FileMappedBuffer::size() const {
    return m_size;
}

bool FileMappedBuffer::empty() const {
    return m_size == 0;
}

}
