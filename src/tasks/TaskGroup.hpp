#pragma once

#include <AsyncLoad/TaskGroup.hpp>
#include <ManagerImpl.hpp>
#include <gtl/phmap.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

struct detail::GroupControl {
    gtl::flat_hash_set<TaskHandle> m_tasks;
    std::atomic<bool> m_closed{false};
    std::atomic<bool> m_eagerDelivery{false};
    std::atomic<TaskGroupFailBehavior> m_failBehavior{TaskGroupFailBehavior::Late};
    std::atomic<GroupStatus> m_status{GroupStatus::Open};
    std::atomic<uint32_t> m_pendingTasks{0};
    Function<void(TaskGroupResult)> m_callback;

    void updateState();
};

}

