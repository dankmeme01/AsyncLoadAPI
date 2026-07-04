#include <AsyncLoad/FileUtils.hpp>
#include <Windows.h>

using namespace geode::prelude;

namespace AsyncLoad {

bool fileExists(ZStringView path) {
    auto attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

Result<CachedBufferChunk> getFileDataImpl(ZStringView path) {
    HANDLE file = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        return Err("Failed to open file '{}', error: {}", path, GetLastError());
    }

    LARGE_INTEGER filesize;
    if (!GetFileSizeEx(file, &filesize)) {
        CloseHandle(file);
        return Err("Failed to get file size for '{}', error: {}", path, GetLastError());
    }

    auto buffer = BufferCache::get().getSized(filesize.QuadPart);
    DWORD bytesRead;
    if (!ReadFile(file, buffer.data(), filesize.QuadPart, &bytesRead, nullptr) || bytesRead != filesize.QuadPart) {
        CloseHandle(file);
        return Err("Failed to read file '{}', error: {}", path, GetLastError());
    }

    CloseHandle(file);
    return Ok(std::move(buffer));
}

Result<FileMappedBuffer> getMappedFileImpl(ZStringView path) {
    HANDLE file = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (file == INVALID_HANDLE_VALUE) {
        return Err("Failed to open file '{}', error: {}", path, GetLastError());
    }

    return FileMappedBuffer::createWithFd(file);
}

Result<> FileMappedBuffer::_map() {
    auto mapping = CreateFileMapping(m_fd, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!mapping) {
        return Err("Failed to create file mapping, error: {}", GetLastError());
    }

    m_ptr = (uint8_t*)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    return Ok();
}

void FileMappedBuffer::_destroy() {
    if (m_ptr) {
        UnmapViewOfFile(m_ptr);
    }

    if (m_fd != INVALID_FD) {
        CloseHandle(m_fd);
    }
}

}
