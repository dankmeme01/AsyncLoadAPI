#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <asp/thread/ThreadPool.hpp>
#include <Geode/utils/StringMap.hpp>
#include "ManagerImpl.hpp"
#include "tasks/ImageTask.hpp"
#include "tasks/TextureTask.hpp"
#include "tasks/SpriteFramesTask.hpp"
#include "tasks/ReadyTask.hpp"

using namespace geode::prelude;

namespace AsyncLoad {

// ALManager


ALManager::~ALManager() {}

ALManager::ALManager() : m_impl(std::make_unique<Impl>()) {
    CCScheduler::get()->scheduleUpdateForTarget(m_impl.get(), 0, false);
}

ALManager& ALManager::get() {
    static auto inst = new ALManager();
    return *inst;
}

TaskHandle ALManager::submitImageLoad(ImageLoadParams&& params) {
    auto control = std::make_shared<ImageTask::Control>(std::move(params.callback));
    auto task = std::make_shared<ImageTask>(std::move(params), control);

    m_impl->submitTask(task);
    return TaskHandle{control};
}

TaskHandle ALManager::submitTextureLoad(TextureLoadParams&& params) {
    auto control = std::make_shared<TextureTask::Control>(std::move(params.callback));
    auto task = std::make_shared<TextureTask>(std::move(params), control);

    m_impl->submitTask(task);
    return TaskHandle{control};
}

TaskHandle ALManager::submitSpriteFramesLoad(SpriteFramesLoadParams&& params) {
    auto control = std::make_shared<SpriteFramesTask::Control>(std::move(params.callback));
    auto task = std::make_shared<SpriteFramesTask>(std::move(params), control);

    m_impl->submitTask(task);
    return TaskHandle{control};
}

TaskHandle ALManager::loadTextureInner(geode::ZStringView path, TextureLoadParams::Callback callback, bool fullPath, bool eager) {
    auto tc = CCTextureCache::get();

    gd::string fp{path.data(), path.size()};
    if (!fullPath) {
        fp = fullPathForFilename(path.view());
    }

    auto cachedTex = static_cast<CCTexture2D*>(tc->m_pTextures->objectForKey(fp));
    if (cachedTex) {
        AL_TRACE("loadTexture: cache hit for {}", fp);

        if (eager) {
            callback(Ok(cachedTex));
            return {};
        } else {
            using RTask = ReadyTask<Ref<CCTexture2D>>;
            // create a dummy task that is already complete
            auto control = std::make_shared<RTask::Control>(std::move(callback));
            auto task = std::make_shared<RTask>(Ok(cachedTex), control);

            m_impl->submitTask(task, true);
            return TaskHandle{control};
        }
    }

    return this->submitTextureLoad(TextureLoadParams{
        .path = fp,
        .isFullPath = true,
        .callback = [cb = std::move(callback), fp = std::move(fp), tc](auto result) mutable {
            if (!result) return cb(std::move(result));

            auto tex = std::move(result).unwrap();
            tc->m_pTextures->setObject(tex, fp);
            cb(Ok(tex));
        },
    });
}

TaskHandle ALManager::loadTexture(ZStringView path, TextureLoadParams::Callback callback, bool fullPath) {
    return loadTextureInner(path, std::move(callback), fullPath, false);
}

TaskHandle ALManager::loadTextureEager(ZStringView path, TextureLoadParams::Callback callback, bool fullPath) {
    return loadTextureInner(path, std::move(callback), fullPath, true);
}

struct PendingSpritesheetState {
    Ref<CCTexture2D> texture;
    std::optional<SpriteFrameData> spriteFrames;
    Function<void(Result<>)> callback;
    std::string name;
};

TaskGroup ALManager::loadSpritesheet(std::string_view name, Function<void(Result<>)> callback) {
    TaskGroup group;
    group.setFailBehavior(TaskGroupFailBehavior::Cancel);

    auto pngPath = fmt::format("{}.png", name);
    auto plistPath = fmt::format("{}.plist", name);

    // cocos in CCSpriteFrameCache uses the raw .plist filename as the key in m_pLoadedFileNames,
    // without running fullPathForFilename. we will replicate this and also use it as a unique key.
    auto plistKey = gd::string{plistPath};
    auto sfc = CCSpriteFrameCache::get();
    if (sfc->m_pLoadedFileNames->contains(plistKey)) {
        // already loaded!
        group.close([cb = std::move(callback)](TaskGroupResult results) mutable {
            if (cb) cb(Ok());
        });
        return group;
    }

    auto pstate = std::make_shared<PendingSpritesheetState>();
    pstate->callback = std::move(callback);
    pstate->name = std::string{plistKey};

    // Load the texture first, since this step may succeed immediately if cached
    auto textureTask = this->loadTextureEager(pngPath, [this, pstate](Result<Ref<CCTexture2D>> result) {
        if (result) {
            pstate->texture = std::move(*result);
        } else {
            log::warn("ALManager: loadSpritesheet texture load failed for {}: {}", pstate->name, result.unwrapErr());
        }
    });
    if (textureTask) {
        group.add(std::move(textureTask));
    }

    // and then start loading the plist in background
    auto framesTask = this->submitSpriteFramesLoad({
        .path = plistPath,
        .isFullPath = false,
        .callback = [this, pstate](Result<SpriteFrameData> result) {
            if (result) {
                pstate->spriteFrames = std::move(*result);
            } else {
                log::warn("ALManager: loadSpritesheet plist load failed for {}: {}", pstate->name, result.unwrapErr());
            }
        },
    });
    group.add(std::move(framesTask));
    group.close([pstate](TaskGroupResult results) mutable {
        if (results.status == GroupStatus::Completed) {
            if (!pstate->texture || !pstate->spriteFrames) {
                // likely one of the subtasks got cancelled
                log::warn("ALManager: loadSpritesheet completed but one of the subtasks did not complete through. Subtask results:");
                for (auto& r : results.results) {
                    log::warn("ALManager: - subtask {}: {} (cancelled: {})", r.handle.name(), r.result, r.cancelled);
                }

                if (pstate->callback) pstate->callback(Err("one or more subtasks failed or was cancelled, see logs"));
                return;
            }

            auto& sf = *pstate->spriteFrames;
            addSpriteFrames(sf, pstate->texture, pstate->name);

            if (pstate->callback) pstate->callback(Ok());
        } else {
            std::string err = "unknown error";
            for (auto& r : results.results) {
                if (r.result && r.result->isErr()) {
                    err = r.result->unwrapErr();
                    break;
                }
            }

            if (pstate->callback) pstate->callback(Err("failed to load spritesheet: {}", err));
        }
    });

    return group;
}

SmartPBO ALManager::requestPBO(size_t capacity) {
#ifdef ENABLE_CACHE
    auto& pbos = m_impl->m_pbos;

    auto it = std::ranges::lower_bound(pbos, capacity, std::less{}, &SmartPBO::capacity);

    size_t maxAcceptable = capacity * 4;
    while (it != pbos.end()) {
        size_t foundCapacity = it->capacity();

        // if the found pbo is more than 4 times bigger than the size we need, don't use it and create a new one
        if (foundCapacity > maxAcceptable) {
            break;
        }

        // if the found PBO is actively being used by the GPU, skip it
        if (it->isBusy()) {
            ++it;
            AL_TRACE("Skipping PBO {} because it is busy", foundCapacity);
            continue;
        }

        // return this pbo
        it->destroyFence();

        auto pbo = std::move(*it);
        pbos.erase(it);
        return pbo;
    }
    AL_TRACE("Allocating new PBO with capacity {}", std::bit_ceil(capacity));
#endif

    return SmartPBO::create(capacity);
}

void ALManager::returnPBO(SmartPBO pbo) {
#ifdef ENABLE_CACHE
    // create a sync fence before returning it
    pbo.createFence();

    // keep the vector sorted
    auto& pbos = m_impl->m_pbos;
    auto it = std::ranges::lower_bound(pbos, pbo.capacity(), std::less{}, &SmartPBO::capacity);
    pbos.insert(it, std::move(pbo));
#endif
}

void ALManager::lendMainThread() {
    m_impl->update(0.f);
}

void ALManager::enqueueSuspendedTask(TaskHandle handle, bool mainThread) {
    m_impl->resumeTask(handle.id(), mainThread);
}

}

$on_game(TexturesUnloaded) {
    auto& m = AsyncLoad::ALManager::Impl::get();
    m.freePBOs();
    m.cancelAll();
}
