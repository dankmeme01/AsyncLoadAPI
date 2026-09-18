#include "ManagerImpl.hpp"
#include "tasks/TaskGroup.hpp"

namespace AsyncLoad {

ALManager::Impl& ALManager::Impl::get() {
    return *ALManager::get().m_impl;
}

ALManager::Impl::Impl() {
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

ALManager::Impl::~Impl() {
    for (auto& thread : m_threads) {
        thread.stop();
    }
    for (auto& thread : m_threads) {
        thread.join();
    }
}

void ALManager::Impl::threadFunc() {
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

void ALManager::Impl::submitTask(std::shared_ptr<Task> task, bool mainThread) {
    // bypass warning about side effects in typeid
    auto& t = *task;

    AL_TRACE("Submitting task {}", task->name());

    auto active = m_activeTasks.lock();
    active->emplace(task->id(), task->control());
    active.unlock();

    if (mainThread) {
        m_MTtaskQueue.push(std::move(task));
    } else {
        m_taskQueue.push(std::move(task));
    }
}

void ALManager::Impl::update(float dt) {
    auto start = asp::Instant::now();

    auto q = std::exchange(*m_groupControlQueue.lock(), {});
    for (auto& ctl : q) {
        ctl->updateState();
    }

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
            m_activeTasks.lock()->erase(task->id());

            // task finished (error / success / cancelled), remove the task and run the callback if it wasn't cancelled
            AL_TRACE("{} finished after {}, state: {}", task->name(), task->elapsed(), task->status());
            if (!task->cancelled()) {
                // invoke success/error callback
                task->invokeCallback();
            }

            task->invokeCompletionHooks();
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

void ALManager::Impl::freePBOs() {
    m_pbos.clear();
}

void ALManager::Impl::cancelAll() {
    auto active = m_activeTasks.lock();
    for (auto& [id, task] : *active) {
        task->cancel();
    }
    active->clear();
}

void ALManager::Impl::enqueueTaskGroupPoll(std::shared_ptr<detail::GroupControl> ctl) {
    auto queue = m_groupControlQueue.lock();
    queue->push_back(std::move(ctl));
}

}
