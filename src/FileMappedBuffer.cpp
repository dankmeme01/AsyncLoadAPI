#include <AsyncLoad/FileMappedBuffer.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

Result<FileMappedBuffer> FileMappedBuffer::createWithFd(FileMappedBufferFd fd) {
    FileMappedBuffer buf;
    buf.m_fd = fd;
    auto res = buf._map();
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
        m_ptr = std::exchange(other.m_ptr, nullptr);
        m_size = std::exchange(other.m_size, 0);
        m_fd = std::exchange(other.m_fd, INVALID_FD);
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
