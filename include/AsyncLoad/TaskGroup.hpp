#pragma once

#include "Task.hpp"

namespace AsyncLoad {

namespace detail {
    struct GroupControl;
}

enum class TaskGroupFailBehavior {
    /// As soon as a single child task encounters an error, the entire group is considered failed.
    /// The callback will be invoked and progress of other tasks will not be cancelled but continue running independently of this group.
    Early,
    /// Similar to `Early`, except a single failure causes the cancellation of all other tasks immediately.
    /// The callback is invoked immediately after cancelling all other tasks.
    Cancel,
    /// Remembers errors of children, but only reports them at the end. As long as there are pending tasks, the group is considered pending.
    Late,
    /// Completely ignores any errors of children. Once no tasks are pending, group is considered successful.
    Ignore,
};

enum class GroupStatus {
    /// Group has not been closed yet and is not tracking tasks
    Open,
    /// Group has been closed and there are still tasks running
    Pending,
    /// No failures were encountered and no tasks are running anymore (cancellation not considered a failure here)
    Completed,
    /// One or more tasks have failed, this is a terminal state and is always set when no more work is being done.
    Failed,
    /// The entire group was cancelled
    Cancelled,
};

struct TaskGroupSingleOutcome {
    TaskHandle handle;
    /// Result of the task, unset if `cancelled` is true.
    std::optional<geode::Result<>> result;
    bool cancelled = false;
};

struct TaskGroupResult {
    GroupStatus status;
    std::vector<TaskGroupSingleOutcome> results;
};

/// Represents a group of tasks that are tracked together.
/// This is a shareable handle, and copying it will return a handle to exact same group.
///
/// The task group has 2 stages of the lifetime with a one-way transition between them:
/// 1. Open: the group was just created, it is not yet actively tracking tasks.
/// Tasks can be added or removed from the group without immediately affecting group state.
/// The group is NOT thread-safe in this state, and must only be interacted with by 1 thread at a time.
///
/// 2. Closed: the group has been irreversibly closed, and is now actively tracking all tasks inside.
/// The status of the group is `Pending` until work is complete or an error is encountered. See `TaskGroupFailBehavior`.
/// Since tasks cannot be added at this point, the group is thread safe and state can be read from multiple threads.
///
/// Note that once a group is closed, dropping this handle does NOT cancel the group, it has to be done explicitly with `cancel()`.
class AL_DLL TaskGroup {
public:
    /// Constructs a task group that tracks no tasks.
    TaskGroup();

    TaskGroup(const TaskGroup&) = default;
    TaskGroup(TaskGroup&&) noexcept = default;
    TaskGroup& operator=(const TaskGroup&) = default;
    TaskGroup& operator=(TaskGroup&&) noexcept = default;

    /// Adds a task to the group. If the group is closed, returns `false`.
    bool add(TaskHandle handle);

    /// Removes a task from the group. If the group is closed or task is not found, returns `false`.
    bool remove(TaskHandle handle);

    /// Cancels all children immediately (calls `cancel` on all handles)
    /// and suppresses the callback of the entire group as well.
    /// If this group is open, performs no action.
    void cancel();

    /// Closes the task group and begins actively tracking all tasks inside. Does nothing if already closed.
    /// The callback, if provided, will be invoked in main thread once the group reaches a terminal state, which depends on the fail behavior.
    /// By default, this means it will be invoked once all tasks reach a terminal state.
    ///
    /// This may invoke the callback eagerly if the group is empty or reaches the terminal state immediately (e.g. a task failed or all are already successful).
    void close(geode::Function<void(TaskGroupResult)> callback = nullptr);

    /// Returns the status of the task group. This is generally defined as:
    /// * For open groups: open
    /// * For empty groups: completed
    /// * If the group was explicitly cancelled: cancelled
    /// * Otherwise, behavior depends on the `TaskGroupFailBehavior` setting (set via `setFailBehavior`):
    /// By default, this is `Late`, meaning that if any child task failed, other tasks are ran to completion and errors are reported at the end.
    ///
    /// If any children get cancelled, it will have no effect on the status of the group, and will be considered the same as success.
    GroupStatus status() const;

    /// Sets the behavior of the group when a child task fails. See `TaskGroupFailBehavior` for details.
    /// This must only be called before closing the group.
    void setFailBehavior(TaskGroupFailBehavior behavior);

    /// Sets whether the delivery of the result should be eager and should avoid waiting extra frames.
    /// By default this is `false`, meaning that if `close` is called when all tasks are complete,
    /// the completion callback will be scheduled to run on main thread next frame.
    ///
    /// By setting this to `true`, you allow the completion callback to be invoked immediately by the same thread that called `close`.
    void setEagerDelivery(bool eager);

private:
    std::shared_ptr<detail::GroupControl> m_ctl;
};

}
