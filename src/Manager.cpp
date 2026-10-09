#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <asp/thread/ThreadPool.hpp>
#include <Geode/utils/StringMap.hpp>
#include "ManagerImpl.hpp"
#include "tasks/ImageTask.hpp"
#include "tasks/TextureTask.hpp"
#include "tasks/SpriteFramesTask.hpp"
#include "tasks/ReadyTask.hpp"
#include "tasks/SpritesheetTask.hpp"

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
    auto cb = std::move(params.callback);
    return m_impl->createAndSubmitTask<ImageTask>(std::move(cb), std::move(params));
}

TaskHandle ALManager::submitTextureLoad(TextureLoadParams&& params) {
    auto cb = std::move(params.callback);
    return m_impl->createAndSubmitTask<TextureTask>(std::move(cb), std::move(params));
}

TaskHandle ALManager::submitSpriteFramesLoad(SpriteFramesLoadParams&& params) {
    auto cb = std::move(params.callback);
    return m_impl->createAndSubmitTask<SpriteFramesTask>(std::move(cb), std::move(params));
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

            m_impl->submitTask(task);
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

TaskHandle ALManager::loadSpritesheet(std::string_view name, Function<void(Result<>)> callback, SpritesheetMergeBehavior mergeBehavior) {
    return m_impl->createAndSubmitTask<SpritesheetTask>(std::move(callback), name, mergeBehavior);
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
