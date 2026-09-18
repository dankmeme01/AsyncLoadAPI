#include <AsyncLoad/TaskGroup.hpp>
#include "TaskGroup.hpp"
#include "TaskImpl.hpp"
#include <ManagerImpl.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

void detail::GroupControl::updateState() {
    auto curStatus = m_status.load(std::memory_order::acquire);

    // we do not care if we are already in a terminal state
    if (curStatus != GroupStatus::Pending) {
        return;
    }

    auto behavior = m_failBehavior.load(std::memory_order::acquire);

    bool anyPending = false;
    bool anyErrored = false;

    for (auto& task : m_tasks) {
        switch (task.status()) {
            case TaskStatus::Pending: {
                anyPending = true;
            } break;
            case TaskStatus::Failed: {
                anyErrored = true;
            } break;
            case TaskStatus::Succeeded:
            case TaskStatus::Cancelled: {
            } break;
        }
    }

    TaskGroupResult result;
    auto pushResult = [&](const TaskHandle& task) {
        AL_DEBUG_ASSERT(task.done());

        TaskGroupSingleOutcome outcome;
        outcome.handle = task;

        auto status = task.status();

        if (status == TaskStatus::Failed) {
            auto e = task.error();
            if (e.empty()) {
                e = "unknown error";
            }
            outcome.result = Err("{} failed: {}", task.name(), e);
        } else if (status == TaskStatus::Succeeded) {
            outcome.result = Ok();
        } else if (status == TaskStatus::Cancelled) {
            outcome.cancelled = true;
        } else {
            AL_DEBUG_ASSERT(false && "invalid task status");
        }

        result.results.push_back(std::move(outcome));
    };

    // handle errors in two cases:
    // 1. any errors occurred and we have early/cancel enabled
    // 2. no more pending tasks, there is at least 1 error, and we do not have Ignore enabled (aka it is Late)
    bool reportErrorEarly = behavior == TaskGroupFailBehavior::Early || behavior == TaskGroupFailBehavior::Cancel;
    if (anyErrored && (reportErrorEarly || (!anyPending && behavior == TaskGroupFailBehavior::Late))) {
        m_status.store(GroupStatus::Failed, std::memory_order::release);

        result.status = GroupStatus::Failed;

        // push all tasks that are in a terminal state
        for (auto& task : m_tasks) {
            if (!task.done()) continue;
            pushResult(task);
        }

        // cancel all tasks if appropriate
        if (behavior == TaskGroupFailBehavior::Cancel) {
            for (auto& task : m_tasks) {
                task.cancel();
            }
        }
    } else if (!anyPending) {
        m_status.store(GroupStatus::Completed, std::memory_order::release);

        result.status = GroupStatus::Completed;

        // all tasks must be terminal now
        for (auto& task : m_tasks) {
            pushResult(task);
        }
    } else {
        return;
    }

    // result
    auto cb = std::move(m_callback);
    if (cb) {
        cb(std::move(result));
    }
}

TaskGroup::TaskGroup() : m_ctl(std::make_shared<detail::GroupControl>()) {}

bool TaskGroup::add(TaskHandle handle) {
    AL_ASSERT(handle);

    if (m_ctl->m_closed.load(std::memory_order::acquire)) {
        return false;
    }

    m_ctl->m_tasks.insert(handle);
    return true;
}

bool TaskGroup::remove(TaskHandle handle) {
    if (m_ctl->m_closed.load(std::memory_order::acquire)) {
        return false;
    }

    return m_ctl->m_tasks.erase(handle) > 0;
}

void TaskGroup::cancel() {
    while (true) {
        auto current = m_ctl->m_status.load(std::memory_order::acquire);
        if (current == GroupStatus::Cancelled || current == GroupStatus::Open) {
            // do nothing
            return;
        }

        if (m_ctl->m_status.compare_exchange_strong(current, GroupStatus::Cancelled, std::memory_order::acq_rel)) {
            break;
        }
    }

    for (auto& task : m_ctl->m_tasks) {
        task.cancel();
    }
}

void TaskGroup::close(Function<void(TaskGroupResult)> callback) {
    bool expected = false;
    if (!m_ctl->m_closed.compare_exchange_strong(expected, true, std::memory_order::acq_rel)) {
        // already closed
        return;
    }

    m_ctl->m_status.store(GroupStatus::Pending, std::memory_order::release);
    m_ctl->m_callback = std::move(callback);

    for (auto& task : m_ctl->m_tasks) {
        task.m_ctl->addCompletionHook([ctl = m_ctl] {
            ctl->updateState();
        });
    }

    // if eager delivery is on, we can update state immediately
    if (m_ctl->m_eagerDelivery.load(std::memory_order::acquire)) {
        m_ctl->updateState();
    } else {
        // otherwise, keep Pending for 1 frame and let main thread poll us soon
        ALManager::Impl::get().enqueueTaskGroupPoll(m_ctl);
    }
}

GroupStatus TaskGroup::status() const {
    return m_ctl->m_status.load(std::memory_order::acquire);
}

void TaskGroup::setFailBehavior(TaskGroupFailBehavior behavior) {
    if (m_ctl->m_closed.load(std::memory_order::acquire)) {
        return;
    }

    m_ctl->m_failBehavior.store(behavior, std::memory_order::release);
}

void TaskGroup::setEagerDelivery(bool eager) {
    if (m_ctl->m_closed.load(std::memory_order::acquire)) {
        return;
    }

    m_ctl->m_eagerDelivery.store(eager, std::memory_order::release);
}

}
