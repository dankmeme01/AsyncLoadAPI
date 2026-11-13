#include <AsyncLoad/FileUtils.hpp>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <unistd.h>
#include <asp/fs.hpp>
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

    size_t size = AAsset_getLength64(asset.get());
    auto buffer = BufferCache::get().getSized(size);
    auto n = AAsset_read(asset.get(), buffer.data(), size);

    if (n < 0) {
        return Err("Failed to read asset '{}'", path);
    } else if (n != size) {
        return Err("short read for asset '{}', read {} bytes, expected {}", path, n, size);
    }

    return Ok(std::move(buffer));
}

static Result<OwnedBuffer> openAndReadAAssetOwned(std::string_view path) {
    auto asset = openAAsset(path, AASSET_MODE_UNKNOWN);
    if (!asset) {
        return Err("Failed to open asset '{}'", path);
    }


    size_t size = AAsset_getLength64(asset.get());
    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(size);
    auto n = AAsset_read(asset.get(), buffer.get(), size);

    if (n < 0) {
        return Err("Failed to read asset '{}'", path);
    } else if (n != size) {
        return Err("short read for asset '{}', read {} bytes, expected {}", path, n, size);
    }

    return Ok(std::move(buffer));
}

static Result<CachedBufferChunk> openAndReadDisk(const char* path) {
    auto file = GEODE_UNWRAP(asp::fs::File::open(path));
    auto size = GEODE_UNWRAP(file.size());

    auto buffer = BufferCache::get().getSized(size);
    GEODE_UNWRAP(file.readExact(std::span{buffer.data(), size}));

    return Ok(std::move(buffer));
}

static Result<OwnedBuffer> openAndReadDiskOwned(const char* path) {
    auto file = GEODE_UNWRAP(asp::fs::File::open(path));
    auto size = GEODE_UNWRAP(file.size());

    auto buffer = std::make_unique_for_overwrite<uint8_t[]>(size);
    GEODE_UNWRAP(file.readExact(std::span{buffer.get(), size}));

    return Ok(OwnedBuffer {
        std::move(buffer),
        static_cast<size_t>(size)
    });
}

bool impl::fileExists(ZStringView path) {
    asp::fs::PathView p{path.view()};
    if (p.isAbsolute()) {
        // absolute path
        auto meta = p.metadata();
        return meta && meta.unwrap().isFile();
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

Result<CachedBufferChunk> impl::getFileData(ZStringView path) {
    std::string_view sv{path};
    if (!sv.empty() && sv[0] == '/') {
        // absolute path, read from disk
        return openAndReadDisk(path.c_str());
    } else {
        // relative path, read from .apk
        return openAndReadAAsset(path);
    }
}

Result<OwnedBuffer> impl::getFileDataOwned(ZStringView path) {
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

static Result<FileMappedBuffer> tryMapAsset(AAsset* asset) {
    off64_t start, length;
    int fd = AAsset_openFileDescriptor64(asset, &start, &length);
    if (fd < 0) {
        return Err("file cannot be mmaped, it is compressed");
    }

    return FileMappedBuffer::createWithFileOffset(asp::fs::File::fromFd(fd), start, length);
}

Result<FileMappedBuffer> impl::getMappedFile(ZStringView path) {
    if (!asp::fs::PathView{path.view()}.isAbsolute()) {
        // relative path
        auto asset = openAAsset(path, AASSET_MODE_UNKNOWN);
        if (!asset) {
            return Err("Failed to open asset '{}'", path);
        }

        return tryMapAsset(asset.get());
    }

    auto file = GEODE_UNWRAP(asp::fs::File::open(path.view()));
    return FileMappedBuffer::createWithFile(std::move(file));
}

Result<FileMetadata> impl::getFileMetadata(geode::ZStringView path) {
    asp::fs::PathView p{path.view()};
    if (p.isAbsolute()) {
        // real file on disk, use fs
        auto meta = GEODE_UNWRAP(p.metadata());
        return Ok(FileMetadata {
            .canMap = meta.isFile(),
            .size = meta.size
        });
    }

    // AAsset
    auto asset = openAAsset(path, AASSET_MODE_UNKNOWN);
    if (!asset) {
        return Err("Failed to open asset '{}'", path);
    }

    off64_t start, length;
    int fd = AAsset_openFileDescriptor64(asset.get(), &start, &length);
    bool canMap = false;

    if (fd >= 0) {
        canMap = true;
        close(fd);
    }

    return Ok(FileMetadata {
        .canMap = canMap,
        .size = static_cast<size_t>(AAsset_getLength64(asset.get()))
    });
}

$on_mod(Loaded) {
    AL_ASSERT(initializeAAssetManager());
}

}
