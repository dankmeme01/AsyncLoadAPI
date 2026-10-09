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
AL_DLL gd::string getPathForFilename(std::string_view filename, std::string_view searchPath);

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

/// Returns whether any texture packs are loaded. This is mostly a heuristic, a `false` return does not guarantee the user is using vanilla resources,
/// but simply that there are no texture packs added through Texture Loader.
AL_DLL bool anyTexturePacksLoaded();

/// Refreshes the cached search paths. This will copy all search paths in CCFileUtils into blaze's internal thread-safe cache,
/// and future calls to `getSearchPaths` will also return the updated search paths.
///
/// This is automatically called in hooks for many CCFileUtils operations like `purgeFileUtils`, `updatePaths`, `addSearchPath`, etc.
/// but if you manually modify the path vector, it is necessary to call this function.
///
/// The cache operation itself is thread-safe, but because this reads the search paths from CCFileUtils,
/// it is only thread safe if you can ensure no one writes to the search paths at the same time (main thread is usually safe).
AL_DLL void refreshSearchPaths();

/// Clears the file utils cache: this includes the path cache and custom provider caches.
/// This does not clear cached search paths unless `paths` is passed as `true`, in that case the function is not thread safe.
AL_DLL void clearFileUtilsCache(bool paths = false);

enum class TextureQuality : uint8_t {
    Low, Medium, High
};

AL_DLL TextureQuality getTextureQuality();

/// Returns quality suffix for the given quality, e.g. "-hd", "-uhd" or ""
AL_DLL std::string_view getQualitySuffix(TextureQuality quality);

struct FileUtilsProvider {
    using ExistsFn = bool(*)(geode::ZStringView);
    using ClearCacheFn = void(*)();
    using GetFileDataFn = geode::Result<CachedBufferChunk>(*)(geode::ZStringView);
    using GetFileDataOwnedFn = geode::Result<OwnedBuffer>(*)(geode::ZStringView);
    using GetMappedFileFn = geode::Result<FileMappedBuffer>(*)(geode::ZStringView);

    size_t _size = sizeof(FileUtilsProvider);

    /// Function to call to check if a file exists by the given full path
    ExistsFn exists{};
    /// Function for additional cleanup, by default does nothing. Called when file utils are purged.
    ClearCacheFn clearCache{};
    /// Function called to get file data by the given full path
    GetFileDataFn getFileData{};
    /// Function called to get file data by the given full path
    GetFileDataOwnedFn getFileDataOwned{};
    /// Function called to get file data by the given full path, using memory mapped io
    GetMappedFileFn getMappedFile{};
};

/// Sets the provider that controls different file related operations.
/// Every pointer that is null in this table means default AsyncLoad operations will be used.
/// The provided table does not need to exist past this call, but function pointers must remain valid.
///
/// Calling this function causes a small memory leak every time, for the previous provider. Avoid calling it many times.
/// If for whatever reason you must call the original functions, see the `AsyncLoad::impl` namespace.
AL_DLL void setFileUtilsProvider(FileUtilsProvider* provider);

namespace impl {
    bool fileExists(geode::ZStringView path);
    geode::Result<CachedBufferChunk> getFileData(geode::ZStringView path);
    geode::Result<OwnedBuffer> getFileDataOwned(geode::ZStringView path);
    geode::Result<FileMappedBuffer> getMappedFile(geode::ZStringView path);
}

AL_DLL size_t getFPFFCacheHits();
AL_DLL size_t getFPFFCacheMisses();
AL_DLL size_t getFPFFCalls();

}
