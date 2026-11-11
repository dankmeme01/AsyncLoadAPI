#pragma once
#include "TaskImpl.hpp"
#include <AsyncLoad/Manager.hpp>

namespace AsyncLoad {

/// A task that is suspended until another task completes
template <
    typename OtherTask,
    typename T = typename OtherTask::Output
>
struct JoinTask final : CrtpTask<JoinTask<OtherTask, T>, T> {
    using OtherControl = typename OtherTask::Control;
    using Control = TypedTask<T>::Control;

    JoinTask(std::shared_ptr<OtherControl> other, std::shared_ptr<Control> ctl) : CrtpTask<JoinTask<OtherTask, T>, T>(std::move(ctl)) {
        other->addCompletionHook([ctl = this->m_ctl, handle = this->handle()](geode::Result<T> result) {
            ctl->complete(std::move(result));
            ALManager::get().enqueueSuspendedTask(handle, true);
        });
    }

    TaskAdvanceResult advance(bool mainThread) override {
        return this->pending() ? TaskAdvanceResult::Suspend : TaskAdvanceResult::Finished;
    }
};

}
