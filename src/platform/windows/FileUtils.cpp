#include <AsyncLoad/FileUtils.hpp>
#include <asp/fs.hpp>
#include <Windows.h>

using namespace geode::prelude;

namespace AsyncLoad {

bool impl::fileExists(ZStringView path) {
    asp::fs::PathView p{path.view()};
    auto meta = p.metadata();
    return meta && meta.unwrap().isFile();
}

Result<CachedBufferChunk> impl::getFileData(ZStringView path) {
    auto file = GEODE_UNWRAP(asp::fs::File::open(path.view()));
    auto size = GEODE_UNWRAP(file.size());

    auto buffer = BufferCache::get().getSized(size);
    GEODE_UNWRAP(file.readExact(std::span{buffer.data(), size}));

    return Ok(std::move(buffer));
}

Result<OwnedBuffer> impl::getFileDataOwned(ZStringView path) {
    auto file = GEODE_UNWRAP(asp::fs::File::open(path.view()));
    auto size = GEODE_UNWRAP(file.size());

    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(size);
    GEODE_UNWRAP(file.readExact(std::span{buffer.get(), size}));

    return Ok(OwnedBuffer {
        std::move(buffer),
        static_cast<size_t>(size)
    });
}

Result<FileMappedBuffer> impl::getMappedFile(ZStringView path) {
    auto file = GEODE_UNWRAP(asp::fs::File::open(path.view()));
    return FileMappedBuffer::createWithFile(std::move(file));
}

Result<FileMetadata> impl::getFileMetadata(geode::ZStringView path) {
    auto meta = GEODE_UNWRAP(asp::fs::PathView{path.view()}.metadata());
    return Ok(FileMetadata {
        .canMap = meta.isFile(),
        .size = meta.size
    });
}

Result<> FileMappedBuffer::_map(std::optional<size_t> offset, std::optional<size_t> size) {
    if (size) {
        m_size = *size;
    } else {
        m_size = GEODE_UNWRAP(m_file.size());
    }

    if (m_size == 0) {
        return Ok();
    }

    static size_t granularity = [] {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        return si.dwAllocationGranularity;
    }();

    size_t off = offset.value_or(0);
    size_t delta = off % granularity;
    size_t mapLen = m_size + delta;

    auto mapping = CreateFileMapping(m_file.fd(), nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!mapping) {
        return Err("Failed to create file mapping, error: {}", GetLastError());
    }

    LARGE_INTEGER offsetli;
    offsetli.QuadPart = off - delta;
    auto base = (uint8_t*)MapViewOfFile(mapping, FILE_MAP_READ, offsetli.HighPart, offsetli.LowPart, mapLen);
    CloseHandle(mapping);

    if (!base) {
        return Err("Failed to map view of file, error: {}", GetLastError());
    }

    m_mapBase = base;
    m_mapLen = mapLen;
    m_ptr = base + delta;

    return Ok();
}

void FileMappedBuffer::_destroy() {
    if (m_mapBase) {
        UnmapViewOfFile(m_mapBase);
    }

    m_ptr = nullptr;
    m_size = 0;
    m_mapBase = nullptr;
    m_mapLen = 0;
}

}
