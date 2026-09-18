#pragma once

#include <AsyncLoad/TaskGroup.hpp>
#include "TaskImpl.hpp"
#include <ManagerImpl.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

struct detail::GroupControl {
    std::unordered_set<TaskHandle> m_tasks;
    std::atomic<bool> m_closed{false};
    std::atomic<bool> m_eagerDelivery{false};
    std::atomic<TaskGroupFailBehavior> m_failBehavior{TaskGroupFailBehavior::Late};
    std::atomic<GroupStatus> m_status{GroupStatus::Open};
    Function<void(TaskGroupResult)> m_callback;

    void updateState();
};

}

