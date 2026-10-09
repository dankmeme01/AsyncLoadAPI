#include <AsyncLoad/FileUtils.hpp>
#include <Geode/utils/StringBuffer.hpp>
#include <Geode/modify/CCFileUtils.hpp>
#include <memory>
#include <gtl/phmap.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

namespace impl {
    bool fileExists(ZStringView path);
    Result<CachedBufferChunk> getFileData(ZStringView path);
    Result<OwnedBuffer> getFileDataOwned(ZStringView path);
    Result<FileMappedBuffer> getMappedFile(ZStringView path);
}

static FileUtilsProvider* defaultProvider() {
    auto p = new FileUtilsProvider();
    p->exists = &impl::fileExists;
    p->clearCache = [] {};
    p->getFileData = &impl::getFileData;
    p->getFileDataOwned = &impl::getFileDataOwned;
    p->getMappedFile = &impl::getMappedFile;
    return p;
}

static asp::Mutex<gtl::flat_hash_map<uint64_t, gd::string>> g_cache;
// TODO: libc++ does not implement std::atomic<std::shared_ptr> in 2026, so use a spinlock.
// track: https://github.com/llvm/llvm-project/issues/99980
static asp::SpinLock<std::shared_ptr<std::vector<std::string>>> g_searchPaths;
static std::atomic<bool> g_texturePacks{false};
static std::atomic<size_t> g_cacheHits = 0;
static std::atomic<size_t> g_cacheMisses = 0;
static std::atomic<size_t> g_fpffCalls = 0;
static std::atomic<FileUtilsProvider*> g_provider{defaultProvider()};

static FileUtilsProvider* provider() {
    return g_provider.load(std::memory_order::acquire);
}

void setFileUtilsProvider(FileUtilsProvider* provider) {
    auto newp = defaultProvider();

    auto copy = [&](auto... members) {
        (([&] {
            if (auto fn = provider->*members) {
                newp->*members = fn;
            }
        }()), ...);
    };

    copy(
        &FileUtilsProvider::exists,
        &FileUtilsProvider::clearCache,
        &FileUtilsProvider::getFileData,
        &FileUtilsProvider::getFileDataOwned,
        &FileUtilsProvider::getMappedFile
    );

    // ignore previous value, there is no feasible way to safely free it without locks
    g_provider.store(newp, std::memory_order::release);
}

/// See platform/shared_apple/FileUtils.hpp
#if defined(__APPLE__)
std::string getPathForDirAndFilenameImpl(geode::ZStringView directory, geode::ZStringView filename);
#endif


struct HookedFileUtils : public Modify<HookedFileUtils, CCFileUtils> {
    static HookedFileUtils& get() {
        return *static_cast<HookedFileUtils*>(CCFileUtils::get());
    }

    $override
    void purgeCachedEntries() {
        CCFileUtils::purgeCachedEntries();
        doCleanup();
    }

    $override
    static void purgeFileUtils() {
        CCFileUtils::purgeFileUtils();
        doCleanup(true);
    }

    // Many of those methods may call each other, avoid cloning unnecessary by using a raii variable
    static inline thread_local size_t s_depth = 0;
    struct ClonePathsRaii {
        ClonePathsRaii() {
            ++s_depth;
        }

        ~ClonePathsRaii() {
            --s_depth;
            if (s_depth == 0) {
                HookedFileUtils::get().cloneSearchPaths();
            }
        }
    };

    $override
    void setSearchPaths(const gd::vector<gd::string>& searchPaths) {
        ClonePathsRaii _;
        CCFileUtils::setSearchPaths(searchPaths);
    }

    $override
    void addSearchPath(const char* path) {
        ClonePathsRaii _;
        CCFileUtils::addSearchPath(path);
    }

    $override
    void removeSearchPath(const char *path) {
        ClonePathsRaii _;
        CCFileUtils::removeSearchPath(path);
    }

    static void updatePathsDetour(HookedFileUtils* self) {
        ClonePathsRaii _;
        self->updatePaths();
    }

#ifndef __APPLE__
    $override
    void removeAllPaths() {
        ClonePathsRaii _;
        CCFileUtils::removeAllPaths();
    }
#endif

    void cloneSearchPaths() {
        auto paths = this->getSearchPaths();
        AL_TRACE("Cloning search paths ({} paths)", paths.size());

        auto vec = std::make_shared<std::vector<std::string>>();
        vec->reserve(paths.size());
        for (const auto& p : paths) {
            vec->emplace_back(p);
        }

        *g_searchPaths.lock() = std::move(vec);
        g_texturePacks.store(this->getTexturePackCount() > 0, std::memory_order::relaxed);
    }

    static void doCleanup(bool searchPaths = false) {
        g_cache.lock()->clear();
        if (searchPaths) {
            *g_searchPaths.lock() = std::make_shared<std::vector<std::string>>();
        }

        provider()->clearCache();
    }
};

void refreshSearchPaths() {
    HookedFileUtils::get().cloneSearchPaths();
}

void clearFileUtilsCache(bool paths) {
    HookedFileUtils::doCleanup(false);

    if (paths) {
        refreshSearchPaths();
    }
}

bool fileExists(geode::ZStringView path) {
    return provider()->exists(path);
}

gd::string getPathForFilename(std::string_view filename, std::string_view searchPath) {
    // unused
    std::string_view resolutionDirectory;

    std::string_view file = filename;
    std::string_view filePath;

    size_t slashPos = file.find_last_of('/');
    if (slashPos != std::string::npos) {
        filePath = file.substr(0, slashPos + 1);
        file = file.substr(slashPos + 1);
    }

    utils::StringBuffer<512> buf;
    buf.append("{}{}{}", searchPath, filePath, resolutionDirectory);

#ifndef __APPLE__
    buf.append(file);

    if (fileExists(buf.c_str())) {
        return gd::string(buf.data(), buf.size());
    }
    return gd::string{};
#else
    // Apple quirks
    utils::StringBuffer<512> fileBuf;
    fileBuf.append("{}", file);
    return getPathForDirAndFilenameImpl(buf.c_str(), fileBuf.c_str());
#endif
}


TextureQuality getTextureQuality() {
    float sf = CCDirector::get()->getContentScaleFactor();
    if (sf >= 4.f) {
        return TextureQuality::High;
    } else if (sf >= 2.f) {
        return TextureQuality::Medium;
    } else {
        return TextureQuality::Low;
    }
}

static uint64_t fnv1aHash(std::string_view s) {
    uint64_t hash = 0xcbf29ce484222325;
    for (char c : s) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 0x100000001b3;
    }
    return hash;
}

// returns the quality suffix for the given quality, e.g. "", "-hd", "-uhd"
std::string_view getQualitySuffix(TextureQuality quality) {
    switch (quality) {
        case TextureQuality::Low: {
            return "";
        } break;
        case TextureQuality::Medium: {
            return "-hd";
        } break;
        case TextureQuality::High: {
            return "-uhd";
        } break;
    }
}

static void cachePath(uint64_t hash, const gd::string& path) {
    g_cache.lock()->emplace(hash, path);
}

static gd::string getCachedPath(uint64_t hash) {
    auto cache = g_cache.lock();
    auto it = cache->find(hash);
    if (it != cache->end()) {
        return it->second;
    }
    return {};
}

gd::string fullPathForFilename(std::string_view input, bool ignoreSuffix) {
    return fullPathForFilenameWithSuffix(
        input,
        ignoreSuffix ? std::nullopt : std::optional{getQualitySuffix(getTextureQuality())}
    );
}

gd::string fullPathForFilenameWithSuffix(std::string_view input, std::optional<std::string_view> applySuffix) {
    g_fpffCalls.fetch_add(1, std::memory_order::relaxed);

    if (input.empty()) {
        return {};
    }

    // if the input is an absolute path, return it as is
    // we try to make this check as cheap as possible, so don't rely on std::filesystem or cocos
#ifdef GEODE_IS_WINDOWS
    if (input.size() >= 3 && std::isalpha(input[0]) && input[1] == ':' && (input[2] == '/' || input[2] == '\\')) {
        return gd::string{input};
    } else if (input.size() >= 2 && input[0] == '\\' && input[1] == '\\') {
        return gd::string{input};
    }
#else
    if (input.size() >= 1 && input[0] == '/') {
        return gd::string{input.data(), input.size()};
    }
#endif

    // try to find the string in cache
    auto hash = fnv1aHash(input);
    if (applySuffix) {
        hash ^= fnv1aHash(*applySuffix);
    }

    auto cached = getCachedPath(hash);
    if (!cached.empty()) {
        g_cacheHits.fetch_add(1, std::memory_order::relaxed);
        return cached;
    }

    g_cacheMisses.fetch_add(1, std::memory_order::relaxed);

    auto& fu = HookedFileUtils::get();

    // add the quality suffix if needed
    utils::StringBuffer<1024> filenameBuf;

    if (applySuffix) {
        auto period = input.find_last_of('.');
        std::string_view base = input.substr(0, period);

        bool hasQualitySuffix = base.ends_with("-uhd") || base.ends_with("-hd");

        if (!hasQualitySuffix) {
            filenameBuf.append(base);
            filenameBuf.append(*applySuffix);

            if (period != std::string::npos) {
                std::string_view extension = input.substr(period);
                filenameBuf.append(extension);
            }
        }
    }

    if (filenameBuf.size() == 0) {
        filenameBuf.append(input);
    }

    // we disregard CCFileUtils m_pFilenameLookupDict / getNewFilename() here,
    // as nobody really uses it and it'd be a pain

    auto filename = filenameBuf.view();

    // we discard resolution directories here, since no one uses them

    // try all search paths
    auto searchPaths = *g_searchPaths.lock();
    AL_ASSERT(searchPaths);

    for (const auto& sp : *searchPaths) {
        auto fp = getPathForFilename(filename, sp);
        if (!fp.empty()) {
            cachePath(hash, fp);
            return fp;
        }
    }

    gd::string ret;
    if (applySuffix == "-uhd") {
        // try to downgrade and see if there's an -hd texture
        ret = fullPathForFilenameWithSuffix(input, "-hd");
    } else if (applySuffix) {
        // hd fails, try to find the same file without any quality suffix
        ret = fullPathForFilenameWithSuffix(input, std::nullopt);
    } else {
        // if all else fails, accept defeat
        ret = gd::string{filename.data(), filename.size()};
    }

    cachePath(hash, ret);
    return ret;
}

Result<CachedBufferChunk> getFileData(
    ZStringView path,
    bool assumeFullPath
) {
    if (path.empty()) {
        return Err("Empty path passed to getFileData");
    }

    auto prov = provider();
    if (assumeFullPath) {
        return prov->getFileData(path);
    }

    auto p = fullPathForFilename(path);
    return prov->getFileData(p);
}

Result<OwnedBuffer> getFileDataOwned(
    ZStringView path,
    bool assumeFullPath
) {
    if (path.empty()) {
        return Err("Empty path passed to getFileDataOwned");
    }

    auto prov = provider();
    if (assumeFullPath) {
        return prov->getFileDataOwned(path);
    }

    auto p = fullPathForFilename(path);
    return prov->getFileDataOwned(p);
}

Result<FileMappedBuffer> getMappedFile(
    ZStringView path,
    bool assumeFullPath
) {
    if (path.empty()) {
        return Err("Empty path passed to getMappedFile");
    }

    auto prov = provider();
    if (assumeFullPath) {
        return prov->getMappedFile(path);
    }

    auto p = fullPathForFilename(path);
    return prov->getMappedFile(p);
}

std::shared_ptr<std::vector<std::string>> getSearchPaths() {
    return *g_searchPaths.lock();
}

bool anyTexturePacksLoaded() {
    return g_texturePacks.load(std::memory_order::relaxed);
}

size_t getFPFFCacheHits() {
    return g_cacheHits.load(std::memory_order::relaxed);
}

size_t getFPFFCacheMisses() {
    return g_cacheMisses.load(std::memory_order::relaxed);
}

size_t getFPFFCalls() {
    return g_fpffCalls.load(std::memory_order::relaxed);
}

$on_mod(Loaded) {
    // updatePaths is a Geode.dll function, we must hook it
    // scary geode hook!
    auto func = getNonVirtual(&CCFileUtils::updatePaths);
    auto result = Mod::get()->hook(
        reinterpret_cast<void*>(func),
        &HookedFileUtils::updatePathsDetour,
        "cocos2d::CCFileUtils::updatePaths",
        tulip::hook::TulipConvention::Thiscall
    );
    AL_ASSERT(result.isOk() && "failed to hook updatePaths");

    // must be done on iOS, safe for other platforms to do it redundantly too
    refreshSearchPaths();
}

}
