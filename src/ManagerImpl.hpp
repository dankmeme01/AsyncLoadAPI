#pragma once
#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <AsyncLoad/TaskGroup.hpp>
#include <asp/thread/ThreadPool.hpp>
#include <Geode/utils/StringMap.hpp>
#include "tasks/TaskImpl.hpp"

using namespace geode::prelude;

namespace AsyncLoad {

struct ALManager::Impl : CCObject {
    std::vector<asp::Thread<>> m_threads;
    asp::Mutex<std::unordered_map<uint64_t, std::shared_ptr<detail::TaskControlBase>>> m_activeTasks;
    asp::Channel<std::shared_ptr<Task>> m_taskQueue;
    asp::Channel<std::shared_ptr<Task>> m_MTtaskQueue;
    asp::Mutex<std::deque<std::shared_ptr<detail::GroupControl>>> m_groupControlQueue;
    std::vector<SmartPBO> m_pbos;

    Impl();
    ~Impl();

    static Impl& get();

    void threadFunc();
    void submitTask(std::shared_ptr<Task> task, bool mainThread = false);
    void update(float dt);

    void freePBOs();
    void cancelAll();

    void enqueueTaskGroupPoll(std::shared_ptr<detail::GroupControl> ctl);
};

}
