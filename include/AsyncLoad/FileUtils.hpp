#pragma once
#include "util/BufferCache.hpp"
#include "util/FileMappedBuffer.hpp"
#include "util/BufferCache.hpp"
#include <Geode/utils/ZStringView.hpp>
#include <memory>

#include "util/EventDefine.hpp"

namespace AsyncLoad {

/// Rewrite of CCFileUtils::isFileExist that does not open any files, is fast and correct,
/// unlike cocos which may fail and return false on relative paths on apple.
///
/// This is for files specifically, it will fail for directories but will follow symlinks.
///
/// Note: this requires the path to be a full path already (not necessarily absolute, but a result of `fullPathForFilename`).
AL_EVENT_EXPORT(bool, fileExists, (geode::ZStringView path), (path));

/// Rewrite of CCFileUtils::getPathForFilename, saner than cocos.
/// This is a somewhat internal function, you likely want `fullPathForFilename` instead.
AL_EVENT_EXPORT(std::string, getPathForFilename, (std::string_view filename, std::string_view searchPath), (filename, searchPath));


/// Rewrite of CCFileUtils::fullPathForFilename, much faster and completely thread-safe.
AL_EVENT_EXPORT(std::string, fullPathForFilename, (std::string_view input, bool ignoreSuffix = false), (input, ignoreSuffix));
AL_EVENT_EXPORT(std::string, fullPathForFilenameWithSuffix, (std::string_view input, std::optional<std::string_view> applySuffix), (input, applySuffix));

/// Rewrite of CCFileUtils::getFileData, fast and thread safe, and with good error reporting.
AL_EVENT_EXPORT(geode::Result<CachedBufferChunk>, getFileData, (
    geode::ZStringView path,
    bool assumeFullPath = false
), (path, assumeFullPath));

struct OwnedBuffer {
    std::unique_ptr<uint8_t[]> data;
    size_t size;

    std::span<uint8_t> span() const {
        return std::span<uint8_t>(data.get(), size);
    }
};

/// Rewrite of CCFileUtils::getFileData, fast and thread safe, and with good error reporting.
/// Unlike `getFileData`, returns an `OwnedBuffer` instead of a `CachedBufferChunk`.
AL_EVENT_EXPORT(geode::Result<OwnedBuffer>, getFileDataOwned, (
    geode::ZStringView path,
    bool assumeFullPath = false
), (path, assumeFullPath));

/// Alternative to getFileData that does not read the entire file into memory, instead using memory mapping.
/// Note that on some platforms, not all files can be mapped. Call `AsyncLoad::canMapFile` to verify.
AL_EVENT_EXPORT(geode::Result<FileMappedBuffer>, getMappedFile, (
    geode::ZStringView path,
    bool assumeFullPath = false
), (path, assumeFullPath));

struct FileMetadata {
    /// Whether the file can be mmapped, and `getMappedFile` is *expected to* succeed.
    /// This will be true for most assets on most platforms, like `.png` files.
    /// It's only false on Android for assets that are compressed in the APK file.
    bool canMap;
    /// Size of the file in bytes
    uint64_t size;
};

/// Returns the metadata of the file, or an error if the file is not found
AL_EVENT_EXPORT(geode::Result<FileMetadata>, getFileMetadata, (
    geode::ZStringView path,
    bool assumeFullPath = false
), (path, assumeFullPath));

/// Gets the cocos search paths in a thread-safe way, allowing you to manually do file/path interactions,
/// without needing the main thread to access CCFileUtils.
AL_EVENT_EXPORT(std::shared_ptr<std::vector<std::string>>, getSearchPaths, (), ());

/// Returns whether any texture packs are loaded. This is mostly a heuristic, a `false` return does not guarantee the user is using vanilla resources,
/// but simply that there are no texture packs added through Texture Loader.
AL_EVENT_EXPORT(bool, anyTexturePacksLoaded, (), ());

/// Refreshes the cached search paths. This will copy all search paths in CCFileUtils into blaze's internal thread-safe cache,
/// and future calls to `getSearchPaths` will also return the updated search paths.
///
/// This is automatically called in hooks for many CCFileUtils operations like `purgeFileUtils`, `updatePaths`, `addSearchPath`, etc.
/// but if you manually modify the path vector, it is necessary to call this function.
///
/// The cache operation itself is thread-safe, but because this reads the search paths from CCFileUtils,
/// it is only thread safe if you can ensure no one writes to the search paths at the same time (main thread is usually safe).
AL_EVENT_EXPORT(void, refreshSearchPaths, (), ());

/// Clears the file utils cache: this includes the path cache and custom provider caches.
/// This does not clear cached search paths unless `paths` is passed as `true`, in that case the function is not thread safe.
AL_EVENT_EXPORT(void, clearFileUtilsCache, (bool paths = false), (paths));

enum class TextureQuality : uint8_t {
    Low, Medium, High
};

AL_EVENT_EXPORT(TextureQuality, getTextureQuality, (), ());

/// Returns quality suffix for the given quality, e.g. "-hd", "-uhd" or ""
AL_EVENT_EXPORT(std::string_view, getQualitySuffix, (TextureQuality quality), (quality));

struct FileUtilsProvider {
    using ExistsFn = bool(*)(geode::ZStringView);
    using ClearCacheFn = void(*)();
    using GetFileDataFn = geode::Result<CachedBufferChunk>(*)(geode::ZStringView);
    using GetFileDataOwnedFn = geode::Result<OwnedBuffer>(*)(geode::ZStringView);
    using GetMappedFileFn = geode::Result<FileMappedBuffer>(*)(geode::ZStringView);
    using GetFileMetadataFn = geode::Result<FileMetadata>(*)(geode::ZStringView);

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
    /// Function called to get file metadata by the given full path
    GetFileMetadataFn getFileMetadata{};
};

/// Sets the provider that controls different file related operations.
/// Every pointer that is null in this table means default AsyncLoad operations will be used.
/// The provided table does not need to exist past this call, but function pointers must remain valid.
///
/// Calling this function causes a small memory leak every time, for the previous provider. Avoid calling it many times.
/// If for whatever reason you must call the original functions, see the `AsyncLoad::impl` namespace.
AL_EVENT_EXPORT(void, setFileUtilsProvider, (FileUtilsProvider* provider), (provider));

namespace impl {
    AL_DLL bool fileExists(geode::ZStringView path);
    AL_DLL geode::Result<CachedBufferChunk> getFileData(geode::ZStringView path);
    AL_DLL geode::Result<OwnedBuffer> getFileDataOwned(geode::ZStringView path);
    AL_DLL geode::Result<FileMappedBuffer> getMappedFile(geode::ZStringView path);
    AL_DLL geode::Result<FileMetadata> getFileMetadata(geode::ZStringView path);
}

AL_EVENT_EXPORT(size_t, getFPFFCacheHits, (), ());
AL_EVENT_EXPORT(size_t, getFPFFCacheMisses, (), ());
AL_EVENT_EXPORT(size_t, getFPFFCalls, (), ());

}
