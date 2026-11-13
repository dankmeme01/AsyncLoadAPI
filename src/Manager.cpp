#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <asp/thread/ThreadPool.hpp>
#include <Geode/utils/StringMap.hpp>
#include "ManagerImpl.hpp"
#include "tasks/ImageTask.hpp"
#include "tasks/TextureTask.hpp"
#include "tasks/SpriteFramesTask.hpp"
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
    return m_impl->createAndSubmitTask<TextureTask>(std::move(cb), std::move(params), false);
}

TaskHandle ALManager::submitSpriteFramesLoad(SpriteFramesLoadParams&& params) {
    auto cb = std::move(params.callback);
    return m_impl->createAndSubmitTask<SpriteFramesTask>(std::move(cb), std::move(params));
}

TaskHandle ALManager::loadTextureInner(std::string_view path, TextureLoadParams::Callback callback, bool fullPath, bool eager) {
    auto tc = CCTextureCache::get();

    std::string fp{path};
    if (!fullPath) {
        fp = fullPathForFilename(path);
    }

    auto cachedTex = static_cast<CCTexture2D*>(tc->m_pTextures->objectForKey(fp));
    if (cachedTex) {
        AL_TRACE("loadTexture: cache hit for '{}'", fp);

        if (eager) {
            callback(Ok(cachedTex));
            return {};
        } else {
            // create a dummy task that is already complete
            return m_impl->createReadyTask<Ref<CCTexture2D>>(Ok(cachedTex), std::move(callback));
        }
    }

    // check if it's already pending and return a JoinTask
    if (auto existingTask = m_impl->findTextureTask(fp)) {
        auto task = m_impl->createJoinTask<TextureTask>(existingTask, std::move(callback));
        AL_TRACE("loadTexture: task {} will join existing task {} for texture '{}'", task.id(), existingTask->m_id, fp);
        return task;
    }

    AL_TRACE_NOISY("loadTexture: creating task for texture '{}'", fp);
    return m_impl->createAndSubmitTask<TextureTask>(
        [cb = std::move(callback), fp, tc](auto result) mutable {
            if (!result) return cb(std::move(result));

            auto tex = std::move(result).unwrap();
            tc->m_pTextures->setObject(tex, fp);
            cb(Ok(tex));
        },
        TextureLoadParams{
            .path = fp,
            .isFullPath = true,
        },
        true
    );
}

TaskHandle ALManager::loadSpritesheetInner(
    std::string_view name,
    LoadSpritesheetCallback callback,
    SpritesheetMergeBehavior mergeBehavior,
    bool eager
) {
    auto sfc = CCSpriteFrameCache::get();

    // cocos in CCSpriteFrameCache uses the raw .plist filename as the key in m_pLoadedFileNames,
    // without running fullPathForFilename. we will replicate this and also use it as a unique key.
    auto plistKey = fmt::format("{}.plist", name);
    if (sfc->m_pLoadedFileNames->contains(plistKey)) {
        // already loaded!
        AL_TRACE("loadSpritesheet: cache hit for {}", plistKey);
        LoadedSpritesheet result {
            .skipped = true
        };

        if (eager) {
            callback(Ok(std::move(result)));
            return {};
        } else {
            // create a dummy task that is already complete
            return m_impl->createReadyTask<LoadedSpritesheet>(Ok(std::move(result)), std::move(callback));
        }
    }

    // check if it's already pending and return a JoinTask
    if (auto existingTask = m_impl->findSpritesheetTask(name)) {
        auto task = m_impl->createJoinTask<SpritesheetTask>(existingTask, std::move(callback));
        AL_TRACE("loadSpritesheet: task {} will join existing task {} for sheet '{}'", task.id(), existingTask->m_id, name);
        return task;
    }

    return m_impl->createAndSubmitTask<SpritesheetTask>(std::move(callback), name, mergeBehavior);
}

TaskHandle ALManager::loadTexture(std::string_view path, TextureLoadParams::Callback callback, bool fullPath) {
    return loadTextureInner(path, std::move(callback), fullPath, false);
}

TaskHandle ALManager::loadTextureEager(std::string_view path, TextureLoadParams::Callback callback, bool fullPath) {
    return loadTextureInner(path, std::move(callback), fullPath, true);
}

TaskHandle ALManager::loadSpritesheet(std::string_view name, LoadSpritesheetCallback callback, SpritesheetMergeBehavior mergeBehavior) {
    return loadSpritesheetInner(name, std::move(callback), mergeBehavior, false);
}

TaskHandle ALManager::loadSpritesheetEager(std::string_view name, LoadSpritesheetCallback callback, SpritesheetMergeBehavior mergeBehavior) {
    return loadSpritesheetInner(name, std::move(callback), mergeBehavior, true);
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

std::optional<BudgetGrant> ALManager::requestMemoryBudget(uint64_t bytes) {
    auto budget = m_impl->m_memoryBudget.lock();
    if (!budget->waiters.empty()) {
        // can't serve request, there are already waiters
        return std::nullopt;
    }

    if (budget->value >= bytes) {
        budget->value -= bytes;
        return BudgetGrant{bytes};
    }

    return std::nullopt;
}

PendingBudgetGrant ALManager::requestMemoryBudgetOrWait(uint64_t bytes, TaskHandle handle) {
    auto budget = m_impl->m_memoryBudget.lock();
    if (budget->waiters.empty() && budget->value >= bytes) {
        budget->value -= bytes;
        return PendingBudgetGrant{std::make_shared<PendingBudgetGrant::State>(
            BudgetGrant{bytes}
        )};
    }

    // park this task
    auto grantState = std::make_shared<PendingBudgetGrant::State>();
    budget->waiters.push_back({
        .task = std::move(handle.m_ctl),
        .grantState = grantState,
        .amount = bytes
    });

    return PendingBudgetGrant{grantState};
}

void ALManager::_returnMemoryBudget(uint64_t value) {
    auto budget = m_impl->m_memoryBudget.lock();
    budget->value += value;

    // wake up any waiters that can be satisfied
    while (!budget->waiters.empty()) {
        auto& waiter = budget->waiters.front();
        if (budget->value < waiter.amount) {
            break;
        }

        budget->value -= waiter.amount;
        waiter.grantState->grant = BudgetGrant{waiter.amount};

        m_impl->resumeTask(waiter.task->m_id, false);
        budget->waiters.pop_front();
    }
}

void ALManager::lendMainThread() {
    m_impl->update(0.f);
}

void ALManager::enqueueSuspendedTask(TaskHandle handle, bool mainThread) {
    m_impl->resumeTask(handle.id(), mainThread);
}

Result<> loadTexture(
    std::string_view path,
    TextureLoadParams::Callback callback,
    bool fullPath
) {
    ALManager::get().loadTexture(path, std::move(callback), fullPath);
    return Ok();
}

Result<> preload(std::string_view path) {
    ALManager::get().loadTextureEager(path, [](auto) {});
    return Ok();
}

Ref<CCTexture2D> getTexture(std::string_view path) {
    std::string p{path};
    if (!p.ends_with(".png")) {
        p += ".png";
    }

    auto fullPath = fullPathForFilename(p);
    return (CCTexture2D*)CCTextureCache::get()->m_pTextures->objectForKey(fullPath);
}

}

$on_game(TexturesUnloaded) {
    auto& m = AsyncLoad::ALManager::Impl::get();
    m.freePBOs();
    m.cancelAll();
}
