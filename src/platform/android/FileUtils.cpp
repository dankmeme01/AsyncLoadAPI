#include <AsyncLoad/FileUtils.hpp>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <Geode/cocos/platform/android/jni/JniHelper.h>

using namespace geode::prelude;

static AAssetManager* g_assetManager = nullptr;

struct AAssetDeleter {
    void operator()(AAsset* asset) const {
        if (asset) {
            AAsset_close(asset);
        }
    }
};

namespace AsyncLoad {

static std::unique_ptr<AAsset, AAssetDeleter> openAAsset(std::string_view path, int mode) {
    if (!g_assetManager) {
        log::warn("AAssetManager not initialized, openAsset({}) will fail!", path);
        return nullptr;
    }

    // strip 'assets/'
    if (path.starts_with("assets/")) {
        path.remove_prefix(7);
    }

    StringBuffer<1024> buf;
    buf.append(path);

    return std::unique_ptr<AAsset, AAssetDeleter>(AAssetManager_open(g_assetManager, buf.c_str(), mode));
}

static Result<CachedBufferChunk> openAndReadAAsset(std::string_view path) {
    auto asset = openAAsset(path, AASSET_MODE_UNKNOWN);
    if (!asset) {
        return Err("Failed to open asset '{}'", path);
    }

    size_t size = AAsset_getLength(asset.get());
    auto buffer = BufferCache::get().getSized(size);
    size_t bytesRead = AAsset_read(asset.get(), buffer.data(), size);
    if (bytesRead != size) {
        return Err("Failed to read asset '{}'", path);
    }

    return Ok(std::move(buffer));
}

static Result<OwnedBuffer> openAndReadAAssetOwned(std::string_view path) {
    auto asset = openAAsset(path, AASSET_MODE_UNKNOWN);
    if (!asset) {
        return Err("Failed to open asset '{}'", path);
    }

    size_t size = AAsset_getLength(asset.get());
    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(size);
    size_t bytesRead = AAsset_read(asset.get(), buffer.get(), size);
    if (bytesRead != size) {
        return Err("Failed to read asset '{}'", path);
    }

    return Ok(std::move(buffer));
}

static Result<std::pair<int, size_t>> doOpen(ZStringView path) {
    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        return Err("Failed to open file '{}', errno: {}", path, errno);
    }

    struct stat fst;
    if (fstat(fd, &fst) == -1) {
        close(fd);
        return Err("Failed to stat file '{}', errno: {}", path, errno);
    }

    return Ok(std::make_pair(fd, fst.st_size));
}

static Result<> readInto(int fd, void* buffer, size_t size) {
    size_t totalRead = 0;
    while (totalRead < size) {
        auto bytesRead = read(fd, (uint8_t*)buffer + totalRead, size - totalRead);
        if (bytesRead == -1) {
            if (errno == EINTR) {
                continue;
            }
            return Err("read failed, errno: {}", errno);
        } else if (bytesRead == 0) {
            return Err("read returned EOF after reading {}/{} bytes", totalRead, size);
        }

        totalRead += bytesRead;
    }

    return Ok();
}

static Result<CachedBufferChunk> openAndReadDisk(const char* path) {
    auto [fd, size] = GEODE_UNWRAP(doOpen(path));
    auto buffer = BufferCache::get().getSized(size);
    auto result = readInto(fd, buffer.data(), size);
    close(fd);

    if (!result) {
        return Err("Failed to read from file '{}': {}", path, result.unwrapErr());
    }

    return Ok(std::move(buffer));
}

static Result<OwnedBuffer> openAndReadDiskOwned(const char* path) {
    auto [fd, size] = GEODE_UNWRAP(doOpen(path));

    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(size);
    auto result = readInto(fd, buffer.get(), size);
    close(fd);

    if (!result) {
        return Err("Failed to read from file '{}': {}", path, result.unwrapErr());
    }

    return Ok(std::move(buffer));
}

bool fileExists(ZStringView path) {
    std::string_view v{path};
    if (!v.empty() && v[0] == '/') {
        // absolute path
        return access(path.c_str(), F_OK) == 0;
    }
    return openAAsset(path.view(), AASSET_MODE_UNKNOWN) != nullptr;
}

bool initializeAAssetManager() {
    if (g_assetManager) return true;

    JniMethodInfo t;
    if (JniHelper::getStaticMethodInfo(t, "org/fmod/FMOD", "getAssetManager", "()Landroid/content/res/AssetManager;")) {
        auto r = t.env->CallStaticObjectMethod(t.classID, t.methodID);
        t.env->DeleteLocalRef(t.classID);
        g_assetManager = AAssetManager_fromJava(t.env, r);
        return g_assetManager != nullptr;
    }
    return false;
}

Result<CachedBufferChunk> getFileDataImpl(ZStringView path) {
    std::string_view sv{path};
    if (!sv.empty() && sv[0] == '/') {
        // absolute path, read from disk
        return openAndReadDisk(path.c_str());
    } else {
        // relative path, read from .apk
        return openAndReadAAsset(path);
    }
}

Result<OwnedBuffer> getFileDataOwnedImpl(ZStringView path) {
    std::string_view sv{path};
    if (!sv.empty() && sv[0] == '/') {
        // absolute path, read from disk
        return openAndReadDiskOwned(path.c_str());
    } else {
        // relative path, read from .apk
        return openAndReadAAssetOwned(path);
    }
}

// mmap implementation

Result<FileMappedBuffer> getMappedFileImpl(ZStringView path) {
    if (!canMapFile(path, true)) {
        return Err("File '{}' cannot be memory mapped, it is not a file on disk", path);
    }

    int fd = open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        return Err("Failed to open file '{}', errno: {}", path, errno);
    }

    return FileMappedBuffer::createWithFd(fd);
}

bool canMapFile(geode::ZStringView path, bool assumeFullPath) {
    if (assumeFullPath) {
        return !path.empty() && *path.begin() == '/';
    }

    auto fullpath = fullPathForFilename(path);
    return !fullpath.empty() && fullpath[0] == '/';
}

Result<> FileMappedBuffer::_map() {
    struct stat s;
    if (fstat(m_fd, &s) == -1) {
        return Err("Failed to stat file '{}', errno: {}", m_fd, errno);
    }

    m_size = s.st_size;
    auto ptr = mmap(nullptr, m_size, PROT_READ, MAP_PRIVATE, m_fd, 0);
    if (ptr == MAP_FAILED) {
        return Err("Failed to mmap file '{}', errno: {}", m_fd, errno);
    }

    m_ptr = (uint8_t*)ptr;
    return Ok();
}

void FileMappedBuffer::_destroy() {
    if (m_ptr) {
        munmap(m_ptr, m_size);
    }

    if (m_fd != INVALID_FD) {
        close(m_fd);
    }
}

$on_mod(Loaded) {
    AL_ASSERT(initializeAAssetManager());
}

}
