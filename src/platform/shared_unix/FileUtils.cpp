#include <Geode/Geode.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <asp/fs.hpp>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>

using namespace geode::prelude;

namespace AsyncLoad {

Result<> FileMappedBuffer::_map(std::optional<size_t> offset, std::optional<size_t> size) {
    if (size) {
        m_size = *size;
    } else {
        m_size = GEODE_UNWRAP(m_file.size());
    }

    if (m_size == 0) {
        return Ok();
    }

    // find the base address to use, which must be aligned to pagesize
    size_t pageSize = sysconf(_SC_PAGESIZE);
    size_t off = offset.value_or(0);
    size_t delta = off % pageSize;
    size_t mapLen = m_size + delta;

    auto ptr = mmap(nullptr, mapLen, PROT_READ, MAP_PRIVATE, m_file.fd(), off - delta);
    if (ptr == MAP_FAILED) {
        return Err("Failed to mmap file '{}', errno: {}", m_file.fd(), errno);
    }

    m_mapBase = (uint8_t*)ptr;
    m_mapLen = mapLen;
    m_ptr = m_mapBase + delta;

    return Ok();
}

void FileMappedBuffer::_destroy() {
    if (m_mapBase) {
        munmap(m_mapBase, m_mapLen);
    }

    m_ptr = nullptr;
    m_size = 0;
    m_mapBase = nullptr;
    m_mapLen = 0;
}

}
