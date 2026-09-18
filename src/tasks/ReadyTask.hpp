#pragma once
#include "TaskImpl.hpp"

namespace AsyncLoad {

/// Small task implementation that is always ready - that means, it already has a result (success or error) on construction.
template <typename T>
struct ReadyTask final : TypedTask<T> {
    using Control = TypedTask<T>::Control;
    ReadyTask(geode::Result<T> value, std::shared_ptr<Control> ctl) : TypedTask<T>(std::move(ctl)) {
        this->complete(std::move(value));
    }

    TaskAdvanceResult advance(bool mainThread = false) override {
        return TaskAdvanceResult::Finished;
    }

    std::string_view typeName() const override {
        return "ReadyTask";
    }
};

}
