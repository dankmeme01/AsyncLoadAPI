#pragma once
#include "util/config.hpp"
#include <stdint.h>
#include <memory>
#include <Geode/utils/function.hpp>

namespace AsyncLoad {

enum class TaskStatus : uint8_t {
    Pending,
    Succeeded,
    Failed,
    Cancelled,
};

namespace detail {
    struct TaskControlBase;
}

class AL_DLL TaskHandle {
public:
    /// Constructs an empty task handle, representing no task.
    TaskHandle() = default;
    TaskHandle(const TaskHandle&) = default;
    TaskHandle(TaskHandle&& other) noexcept = default;
    TaskHandle& operator=(const TaskHandle&) = default;
    TaskHandle& operator=(TaskHandle&& other) noexcept = default;

    bool operator==(const TaskHandle& other) const = default;

    /// Returns whether this handle represents a valid task. This stays `true` after completion and cancellation, and only becomes `false`
    /// if the handle is destroyed or moved away.
    bool valid() const {
        return m_ctl != nullptr;
    }

    operator bool() const {
        return this->valid();
    }

    /// Returns the status of task execution.
    /// Note that the task may change its status soon after polling it, because tasks run asynchronously.
    /// For empty or invalid task handles, returns `Cancelled`.
    TaskStatus status() const;

    /// Returns the unique ID of the task, or 0 for empty task.
    uint64_t id() const;

    /// Returns the error message, if available.
    std::string error() const;

    /// Returns whether the task has reached a conclusion. This can mean one of the three cases: success, error or cancellation.
    bool done() const {
        return this->status() != TaskStatus::Pending;
    }

    /// Returns whether the task has been cancelled.
    bool cancelled() const {
        return this->status() == TaskStatus::Cancelled;
    }

    /// Returns whether the task is still running.
    bool pending() const {
        return this->status() == TaskStatus::Pending;
    }

    /// Cancels the task as soon as possible. This marks the task as cancelled immediately, and `status()` will return `Cancelled`.
    /// Cancellation is always best effort: if the task is currently executing the last step, it may end up running that step to completion.
    ///
    /// However, this function does provide one important guarantee: if called in the main thread, it ensures that
    /// the callbacks of the task will NOT be fired. Even if the task has already completed and the callbacks are queued to run,
    /// this ensures that callbacks will NOT run after `cancel`, so that it's easier to reason about lifetimes of captured objects.
    void cancel() const;

    /// Sets the name of the task, visible in logs and errors for debug purposes.
    /// Safe if you are the only owner of the handle and no one is setting the name at the same time.
    void setName(std::string name) const;

    /// Retrieves the name of the task. If none was set, returns "Task <id>".
    std::string name() const;

private:
    friend class ALManager;
    friend class TaskGroup;
    friend struct std::hash<TaskHandle>;

    std::shared_ptr<detail::TaskControlBase> m_ctl;

    TaskHandle(std::shared_ptr<detail::TaskControlBase> ctl) : m_ctl(std::move(ctl)) {}
};

inline std::string_view format_as(TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending: return "Pending";
        case TaskStatus::Succeeded: return "Succeeded";
        case TaskStatus::Failed: return "Failed";
        case TaskStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

}

namespace std {
    template <>
    struct hash<AsyncLoad::TaskHandle> {
        size_t operator()(const AsyncLoad::TaskHandle& handle) const {
            return std::hash<uint64_t>{}(handle.id());
        }
    };
}
