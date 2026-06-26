#include <AsyncLoad/FileUtils.hpp>
#include <Windows.h>

using namespace geode::prelude;

namespace AsyncLoad {

bool fileExists(ZStringView path) {
    auto attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

Result<std::vector<uint8_t>> getFileDataImpl(ZStringView path) {
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

    auto buffer = std::vector<uint8_t>(filesize.QuadPart);
    DWORD bytesRead;
    if (!ReadFile(file, buffer.data(), filesize.QuadPart, &bytesRead, nullptr) || bytesRead != filesize.QuadPart) {
        CloseHandle(file);
        return Err("Failed to read file '{}', error: {}", path, GetLastError());
    }

    CloseHandle(file);
    return Ok(std::move(buffer));
}

}
