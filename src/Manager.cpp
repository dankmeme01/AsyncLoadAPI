#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <asp/thread/ThreadPool.hpp>
#include "ManagerTask.hpp"

using namespace geode::prelude;

namespace AsyncLoad {

// Task handle

TaskHandle::TaskHandle(uint64_t id) : m_id(id) {}

TaskHandle::~TaskHandle() {
    this->cancel();
}

TaskHandle::TaskHandle(TaskHandle&& other) noexcept{
    m_id = std::exchange(other.m_id, 0);
}

TaskHandle& TaskHandle::operator=(TaskHandle&& other) noexcept {
    if (this != &other) {
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}

void TaskHandle::leak() {
    m_id = 0;
}

void TaskHandle::cancel() {
    if (m_id != 0) {
        ALManager::get().cancelTask(m_id);
    }
}

// ALManager task


// ALManager


struct ALManager::Impl : CCObject {
    std::vector<asp::Thread<>> m_threads;
    asp::Mutex<std::unordered_map<uint64_t, std::shared_ptr<Task>>> m_activeTasks;
    asp::Channel<std::shared_ptr<Task>> m_taskQueue;
    asp::Channel<std::shared_ptr<Task>> m_MTtaskQueue;
    std::vector<SmartPBO> m_pbos;

    Impl() {
        // spawn more threads than available, because some threads may be blocked on IO operations n stuff
        auto nthreads = std::thread::hardware_concurrency() + 4;

        for (size_t i = 0; i < nthreads; ++i) {
            m_threads.emplace_back(asp::Thread<> {[this](auto& st) {
                this->threadFunc();
            }});

            auto& t = m_threads.back();
            t.setName("AsyncLoad worker thread");
            t.start();
        }
    }

    ~Impl() {
        for (auto& thread : m_threads) {
            thread.stop();
        }
        for (auto& thread : m_threads) {
            thread.join();
        }
    }

    void threadFunc() {
        auto opt = m_taskQueue.popTimeout(asp::Duration::fromMillis(500));
        if (!opt) return;

        auto task = std::move(*opt);
        TaskAdvanceResult result;

        do {
            result = task->advance(false);
        } while (result == TaskAdvanceResult::Pending);

        // task has now either finished or requires to be advanced on main thread, so enqueue it
        m_MTtaskQueue.push(std::move(task));
    }

    void submitTask(std::shared_ptr<Task> task) {
        AL_TRACE("Submitting task {} with goal {}", task->m_id, task->m_goal.load());

        auto active = m_activeTasks.lock();
        active->emplace(task->m_id, task);
        active.unlock();

        m_taskQueue.push(std::move(task));
    }

    void cancelTask(uint64_t id) {
        auto active = m_activeTasks.lock();
        auto it = active->find(id);
        if (it != active->end()) {
            // cancel the task and immediately remove from the map, as soon as the current operation finishes,
            // the task should be queued to the main thread and discarded.
            it->second->cancel();
            active->erase(it);
        }
    }

    void update(float dt) {
        auto start = asp::Instant::now();

        while (auto value = m_MTtaskQueue.tryPop()) {
            auto task = std::move(*value);

            bool finished = task->finished();
            bool needsMainThread = true;

            while (!finished && needsMainThread) {
                auto result = task->advance(true);
                switch (result) {
                    case TaskAdvanceResult::Pending: {
                        // should be ran again on a worker thread
                        needsMainThread = false;
                    } break;
                    case TaskAdvanceResult::RequiresMainThread: {
                        // should be ran again here
                    } break;
                    case TaskAdvanceResult::Finished: {
                        // finished!
                        finished = true;
                    } break;
                }
            }

            if (finished) {
                // task finished, remove the task and run the callback if it wasn't cancelled
                AL_TRACE("Task {} finished after {}, state: {}", task->m_id, task->elapsed(), task->state());
                if (!task->cancelled()) {
                    task->invokeCallback();
                    auto active = m_activeTasks.lock();
                    active->erase(task->m_id);
                }
            } else {
                // task needs to continue running but does not require the main thread, so push it back to the worker threads
                m_taskQueue.push(std::move(task));
            }

            // avoid blocking for too long at once, even if a ton of tasks are queued
            // we want to try and avoid huge lag spikes
            if (start.elapsed().millis() > 10) {
                break;
            }
        }

        auto taken = start.elapsed();
        if (taken.millis() > 20) {
            log::warn("ALManager::update took {}", taken);
        } else if (taken.millis() > 2) {
            AL_TRACE("ALManager::update took {}", taken);
        }
    }
};

ALManager::~ALManager() {}

ALManager::ALManager() : m_impl(std::make_unique<Impl>()) {
    CCScheduler::get()->scheduleUpdateForTarget(m_impl.get(), 0, false);
}

ALManager& ALManager::get() {
    static auto inst = new ALManager();
    return *inst;
}

TaskHandle ALManager::submitImageLoad(ImageLoadParams&& params) {
    auto task = std::make_shared<ImageTask>(std::move(params));
    m_impl->submitTask(task);
    return task->handle();
}

TaskHandle ALManager::submitTextureLoad(TextureLoadParams&& params) {
    auto task = std::make_shared<TextureTask>(std::move(params));
    m_impl->submitTask(task);
    return task->handle();
}

TaskHandle ALManager::submitSpriteFramesLoad(SpriteFramesLoadParams&& params) {
    auto task = std::make_shared<SpriteFramesTask>(std::move(params));
    m_impl->submitTask(task);
    return task->handle();
}

TaskHandle ALManager::loadTexture(ZStringView path, TextureLoadParams::Callback callback, bool fullPath) {
    auto tc = CCTextureCache::get();

    gd::string fp{path.data(), path.size()};
    if (!fullPath) {
        fp = fullPathForFilename(path.view());
    }

    auto cachedTex = static_cast<CCTexture2D*>(tc->m_pTextures->objectForKey(fp));
    if (cachedTex) {
        AL_TRACE("loadTexture: cache hit for {}", fp);
        callback(Ok(cachedTex));
        return {};
    }

    return this->submitTextureLoad(TextureLoadParams{
        .path = path,
        .isFullPath = fullPath,
        .callback = [cb = std::move(callback), fp = std::move(fp), tc](auto result) mutable {
            if (!result) return cb(std::move(result));

            auto tex = std::move(result).unwrap();
            tc->m_pTextures->setObject(tex, fp);
            cb(Ok(tex));
        },
    });
}

void ALManager::cancelTask(uint64_t id) {
    m_impl->cancelTask(id);
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

void ALManager::_freePBOs() {
    m_impl->m_pbos.clear();
}

void ALManager::_cancelAll() {
    auto active = m_impl->m_activeTasks.lock();
    for (auto& [id, task] : *active) {
        task->cancel();
    }
    active->clear();
}

void ALManager::lendMainThread() {
    m_impl->update(0.f);
}

}

$on_game(TexturesUnloaded) {
    auto& m = AsyncLoad::ALManager::get();
    m._freePBOs();
    m._cancelAll();
}
