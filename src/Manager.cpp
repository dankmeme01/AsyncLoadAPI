#include <AsyncLoad/Manager.hpp>
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
    std::vector<GLuint> m_pbos;

    Impl() {
        // spawn more threads than available, because some threads may be blocked on IO operations n stuff
        auto nthreads = std::thread::hardware_concurrency() + 4;

        for (size_t i = 0; i < nthreads; ++i) {
            m_threads.emplace_back(asp::Thread<> {[this](auto& st) {
                this->threadFunc();
            }});
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
                if (!task->cancelled()) {
                    task->invokeCallback();
                    auto active = m_activeTasks.lock();
                    active->erase(task->m_id);
                }
            } else {
                // task needs to continue running but does not require the main thread, so push it back to the worker threads
                m_taskQueue.push(std::move(task));
            }
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

void ALManager::cancelTask(uint64_t id) {
    m_impl->cancelTask(id);
}

void ALManager::_retainPBO(uint32_t pbo) {
    m_impl->m_pbos.push_back(pbo);
}

void ALManager::_freePBOs() {
    for (auto pbo : m_impl->m_pbos) {
        glDeleteBuffers(1, &pbo);
    }
    m_impl->m_pbos.clear();
}

}

$on_game(TexturesUnloaded) {
    AsyncLoad::ALManager::get()._freePBOs();
}
