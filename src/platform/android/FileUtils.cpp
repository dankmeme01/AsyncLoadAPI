#include <AsyncLoad/FileUtils.hpp>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <unistd.h>
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

static Result<std::vector<uint8_t>> openAndReadAAsset(std::string_view path) {
    auto asset = openAAsset(path, AASSET_MODE_UNKNOWN);
    if (!asset) {
        return Err("Failed to open asset '{}'", path);
    }

    size_t size = AAsset_getLength(asset.get());
    std::vector<uint8_t> buffer(size);
    size_t bytesRead = AAsset_read(asset.get(), buffer.data(), size);
    if (bytesRead != size) {
        return Err("Failed to read asset '{}'", path);
    }

    return Ok(std::move(buffer));
}

static geode::Result<std::vector<uint8_t>> openAndReadDisk(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        return Err("Failed to open file '{}', errno: {}", path, errno);
    }

    struct stat fst;
    if (fstat(fd, &fst) == -1) {
        close(fd);
        return Err("Failed to stat file '{}', errno: {}", path, errno);
    }

    std::vector<uint8_t> buffer(fst.st_size);
    ssize_t bytesRead = read(fd, buffer.data(), fst.st_size);
    close(fd);

    if (bytesRead != fst.st_size) {
        return Err("Failed to read from file '{}', errno: {}", path, errno);
    }

    return Ok(std::move(buffer));
}

namespace AsyncLoad {

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

Result<std::vector<uint8_t>> getFileDataImpl(ZStringView path) {
    std::string_view sv{path};
    if (!sv.empty() && sv[0] == '/') {
        // absolute path, read from disk
        return openAndReadDisk(path.c_str());
    } else {
        // relative path, read from .apk
        return openAndReadAAsset(path);
    }
}

$on_mod(Loaded) {
    AL_ASSERT(initializeAAssetManager());
}

}
