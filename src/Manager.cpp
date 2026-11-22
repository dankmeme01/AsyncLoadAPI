#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <asp/thread/ThreadPool.hpp>
#include <Geode/utils/StringMap.hpp>
#include <Geode/loader/GameEvent.hpp>
#include "ManagerImpl.hpp"
#include "tasks/ImageTask.hpp"
#include "tasks/TextureTask.hpp"
#include "tasks/SpriteFramesTask.hpp"
#include "OpenGL.hpp"

#include <Geode/modify/LoadingLayer.hpp>

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

TaskHandle loadTexture(std::string_view path, TextureLoadParams::Callback callback, bool fullPath) {
    return ALManager::Impl::get().loadTextureInner(path, std::move(callback), fullPath, false);
}

TaskHandle loadTextureEager(std::string_view path, TextureLoadParams::Callback callback, bool fullPath) {
    return ALManager::Impl::get().loadTextureInner(path, std::move(callback), fullPath, true);
}

TaskHandle loadSpritesheet(std::string_view name, LoadSpritesheetCallback callback, SpritesheetMergeBehavior mergeBehavior) {
    return ALManager::Impl::get().loadSpritesheetInner(name, std::move(callback), mergeBehavior, false);
}

TaskHandle loadSpritesheetEager(std::string_view name, LoadSpritesheetCallback callback, SpritesheetMergeBehavior mergeBehavior) {
    return ALManager::Impl::get().loadSpritesheetInner(name, std::move(callback), mergeBehavior, true);
}

Result<> preload(std::string_view path) {
    loadTextureEager(path, [](auto) {});
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

$on_game(TexturesUnloaded) {
    auto& m = ALManager::Impl::get();
    m.freePBOs();
    m.cancelAll();
}

}

class $modify(LoadingLayer) {
    static void onModify(auto& self) {
        self.setHookPriority("LoadingLayer::init", -1000000).unwrap();
    }

    bool init(bool reload) {
        g_opengl.initialize();
        return LoadingLayer::init(reload);
    }
};
