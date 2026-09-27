#pragma once
#include "util/config.hpp"
#include "util/BufferCache.hpp"
#include "util/FileMappedBuffer.hpp"
#include "util/BufferCache.hpp"
#include <Geode/utils/ZStringView.hpp>
#include <memory>

namespace AsyncLoad {

/// Rewrite of CCFileUtils::isFileExist that does not open any files, is fast and correct,
/// unlike cocos which may fail and return false on relative paths on apple.
///
/// Note: this requires the path to be a full path already (not necessarily absolute, but a result of `fullPathForFilename`).
AL_DLL bool fileExists(geode::ZStringView path);

/// Rewrite of CCFileUtils::getPathForFilename, saner than cocos.
/// This is a somewhat internal function, you likely want `fullPathForFilename` instead.
AL_DLL gd::string getPathForFilename(std::string_view filename, std::string_view resolutionDirectory, std::string_view searchPath);

/// Rewrite of CCFileUtils::fullPathForFilename, much faster and completely thread-safe.
AL_DLL gd::string fullPathForFilename(std::string_view input, bool ignoreSuffix = false);
AL_DLL gd::string fullPathForFilenameWithSuffix(std::string_view input, std::optional<std::string_view> applySuffix);

/// Rewrite of CCFileUtils::getFileData, fast and thread safe, and with good error reporting.
AL_DLL geode::Result<CachedBufferChunk> getFileData(
    geode::ZStringView path,
    bool assumeFullPath = false
);

struct OwnedBuffer {
    std::unique_ptr<uint8_t[]> data;
    size_t size;

    std::span<uint8_t> span() const {
        return std::span<uint8_t>(data.get(), size);
    }
};

/// Rewrite of CCFileUtils::getFileData, fast and thread safe, and with good error reporting.
/// Unlike `getFileData`, returns an `OwnedBuffer` instead of a `CachedBufferChunk`.
AL_DLL geode::Result<OwnedBuffer> getFileDataOwned(
    geode::ZStringView path,
    bool assumeFullPath = false
);

/// Alternative to getFileData that does not read the entire file into memory, instead using memory mapping.
/// Note that on some platforms, not all files can be mapped. Call `AsyncLoad::canMapFile` to verify.
AL_DLL geode::Result<FileMappedBuffer> getMappedFile(
    geode::ZStringView path,
    bool assumeFullPath = false
);

/// Returns whether the file at the given path can be memory mapped, and `getMappedFile` may succeed.
/// This does NOT guarantee it actually *will* succeed, and it does not check actual path existence.
///
/// Right now, this function is equivalent to checking if the file is NOT in APK assets on Android, and simply returns `true` on other platforms.
AL_DLL bool canMapFile(geode::ZStringView path, bool assumeFullPath = false);

/// Gets the cocos search paths in a thread-safe way, allowing you to manually do file/path interactions,
/// without needing the main thread to access CCFileUtils.
AL_DLL std::shared_ptr<std::vector<std::string>> getSearchPaths();

AL_DLL size_t getFPFFCacheHits();
AL_DLL size_t getFPFFCacheMisses();
AL_DLL size_t getFPFFCalls();

}
