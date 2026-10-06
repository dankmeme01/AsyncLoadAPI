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

    switch (result) {
        case TaskAdvanceResult::Pending: std::unreachable();
        case TaskAdvanceResult::RequiresMainThread:
        case TaskAdvanceResult::Finished: {
            m_MTtaskQueue.push(std::move(task));
        } break;
        case TaskAdvanceResult::Suspend: {
            this->suspendTask(std::move(task));
        } break;
    }
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
        bool suspended = false;

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
                case TaskAdvanceResult::Suspend: {
                    // suspend this task to be resumed by someone else
                    needsMainThread = false;
                    suspended = true;
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
        } else if (!suspended) {
            // task needs to continue running but does not require the main thread, so push it back to the worker threads
            m_taskQueue.push(std::move(task));
        } else {
            // task is suspended
            this->suspendTask(std::move(task));
        }

        // avoid blocking for too long at once, even if a ton of tasks are queued
        // we want to try and avoid huge lag spikes
        if (start.elapsed().millis() > 10) {
            break;
        }
    }

    auto taken = start.elapsed();
    if (taken.millis() > 50) {
        log::warn("ALManager::update took {}", taken);
    } else if (taken.millis() > 10) {
        AL_TRACE("ALManager::update took {}", taken);
    }
}

void ALManager::Impl::freePBOs() {
    m_pbos.clear();
}

void ALManager::Impl::cancelAll() {
    m_suspendedTasks.lock()->clear();
    m_earlyResumptions.lock()->clear();

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

void ALManager::Impl::suspendTask(std::shared_ptr<Task> task) {
    // lock order matters - see resumeTask
    auto suspended = m_suspendedTasks.lock();
    auto early = m_earlyResumptions.lock();

    auto it = early->find(task->id());
    if (it != early->end()) {
        // someone enqueued a resumption for this task, do not suspend it
        early->erase(it);
        m_taskQueue.push(std::move(task));
        return;
    }

    // suspend the task to be woken up later
    suspended->emplace(task->id(), std::move(task));
}

void ALManager::Impl::resumeTask(uint64_t id, bool mainThread) {
    auto suspended = m_suspendedTasks.lock();

    auto it = suspended->find(id);
    if (it != suspended->end()) {
        // resume this task
        auto& queue = mainThread ? m_MTtaskQueue : m_taskQueue;
        queue.push(std::move(it->second));
        suspended->erase(it);
        return;
    }

    // if we got here, this means this function was called before a task was fully suspended. there are 2 cases for this:
    // 1. the function is called wrongfully and the task will never suspend
    // 2. the function is called *right before* the task will suspend. for example, the task enqueued async work but it completed instantly.
    // this means this function got called *before* the task returned and a worker thread can put it into the suspend pool.
    //
    // we don't care much about 1st case, but 2nd case is why the early resumptions set exists.
    // both we and the worker thread must hold both locks at once, so no race can occur.
    // we push the task ID into the early resumption set, and the worker will check this set and avoid suspending the task.
    auto early = m_earlyResumptions.lock();
    early->insert(id);
}

}
