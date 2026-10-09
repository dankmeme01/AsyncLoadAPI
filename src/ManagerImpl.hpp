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
    asp::Mutex<std::unordered_map<uint64_t, std::shared_ptr<Task>>> m_suspendedTasks;
    asp::Mutex<std::unordered_set<uint64_t>> m_earlyResumptions;
    asp::Mutex<std::deque<std::shared_ptr<detail::GroupControl>>> m_groupControlQueue;
    std::vector<SmartPBO> m_pbos;

    Impl();
    ~Impl();

    static Impl& get();

    void threadFunc();

    template <typename Task, typename Cb, typename... Args>
    TaskHandle createAndSubmitTask(Cb&& cb, Args&&... args) {
        auto control = std::make_shared<typename Task::Control>(std::forward<Cb>(cb));
        auto task = std::make_shared<Task>(std::forward<Args>(args)..., std::move(control));
        this->submitTask(task);
        return task->handle();
    }

    void submitTask(std::shared_ptr<Task> task);
    void update(float dt);
    void enqueueFinishedTask(std::shared_ptr<Task> task, TaskAdvanceResult advResult, bool mainThread);

    /// Suspends the task if the early resumption set does not contain it
    void suspendTask(std::shared_ptr<Task> task);
    void resumeTask(uint64_t id, bool mainThread);

    void freePBOs();
    void cancelAll();

    void enqueueTaskGroupPoll(std::shared_ptr<detail::GroupControl> ctl);
};

}
