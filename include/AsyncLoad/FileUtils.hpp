#pragma once
#include "Util.hpp"
#include "FileMappedBuffer.hpp"
#include "BufferCache.hpp"
#include <Geode/utils/ZStringView.hpp>
#include <memory>

namespace AsyncLoad {

/// Rewrite of CCFileUtils::isFileExist that does not open any files,
// is fast and correct, unlike cocos which may fail and return false on relative paths on apple.
AL_DLL bool fileExists(geode::ZStringView path);

/// Rewrite of CCFileUtils::getPathForFilename, saner than cocos
AL_DLL gd::string getPathForFilename(std::string_view filename, std::string_view resolutionDirectory, std::string_view searchPath);

/// Rewrite of CCFileUtils::fullPathForFilename, much faster and completely thread-safe.
AL_DLL gd::string fullPathForFilename(std::string_view input, bool ignoreSuffix = false);

/// Rewrite of CCFileUtils::getFileData, fast and thread safe, and with good error reporting.
AL_DLL geode::Result<CachedBufferChunk> getFileData(
    geode::ZStringView path,
    bool assumeFullPath = false
);

/// Alternative to getFileData that does not read the entire file into memory, instead using memory mapping.
AL_DLL geode::Result<FileMappedBuffer> getMappedFile(
    geode::ZStringView path,
    bool assumeFullPath = false
);

AL_DLL size_t getFPFFCacheHits();
AL_DLL size_t getFPFFCacheMisses();
AL_DLL size_t getFPFFCalls();

}
