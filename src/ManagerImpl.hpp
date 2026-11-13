#pragma once
#include <AsyncLoad/Manager.hpp>
#include <AsyncLoad/FileUtils.hpp>
#include <AsyncLoad/TaskGroup.hpp>
#include "tasks/TaskImpl.hpp"
#include "tasks/TextureTask.hpp"
#include "tasks/ReadyTask.hpp"
#include "tasks/JoinTask.hpp"
#include "tasks/SpritesheetTask.hpp"

#include <asp/thread/ThreadPool.hpp>
#include <Geode/utils/StringMap.hpp>
#include <gtl/phmap.hpp>

using namespace geode::prelude;

namespace AsyncLoad {

struct ALManager::Impl : CCObject {
    std::vector<asp::Thread<>> m_threads;
    asp::Mutex<gtl::flat_hash_map<uint64_t, std::shared_ptr<detail::TaskControlBase>>> m_activeTasks;
    asp::Mutex<gtl::flat_hash_map<std::string, std::shared_ptr<TextureTask::Control>>> m_textureTasks;
    asp::Mutex<gtl::flat_hash_map<std::string, std::shared_ptr<SpritesheetTask::Control>>> m_spriteSheetTasks;
    asp::Channel<std::shared_ptr<Task>> m_taskQueue;
    asp::Channel<std::shared_ptr<Task>> m_MTtaskQueue;
    asp::Mutex<gtl::flat_hash_map<uint64_t, std::shared_ptr<Task>>> m_suspendedTasks;
    asp::Mutex<gtl::flat_hash_set<uint64_t>> m_earlyResumptions;
    asp::Mutex<std::deque<std::shared_ptr<detail::GroupControl>>> m_groupControlQueue;

    std::vector<SmartPBO> m_pbos;

    struct MemoryWaiter {
        std::shared_ptr<detail::TaskControlBase> task;
        std::shared_ptr<PendingBudgetGrant::State> grantState;
        uint64_t amount;
    };
    struct MemoryBudget {
        uint64_t value;
        std::deque<MemoryWaiter> waiters;
    };
    asp::Mutex<MemoryBudget> m_memoryBudget;

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

    template <typename T, typename R = ReadyTask<T>, typename Cb>
    TaskHandle createReadyTask(geode::Result<T> result, Cb&& cb) {
        return this->createAndSubmitTask<R>(std::forward<Cb>(cb), std::move(result));
    }

    template <typename Task, typename Cb, typename Control = typename Task::Control>
    TaskHandle createJoinTask(std::shared_ptr<Control> task, Cb&& cb) {
        return this->createAndSubmitTask<JoinTask<Task>>(std::forward<Cb>(cb), std::move(task));
    }

    void submitTask(std::shared_ptr<Task> task);
    void removeTask(const std::shared_ptr<Task>& id);
    void update(float dt);
    void enqueueFinishedTask(std::shared_ptr<Task> task, TaskAdvanceResult advResult, bool mainThread);

    std::shared_ptr<TextureTask::Control> findTextureTask(std::string_view path);
    std::shared_ptr<SpritesheetTask::Control> findSpritesheetTask(std::string_view name);


    /// Suspends the task if the early resumption set does not contain it
    void suspendTask(std::shared_ptr<Task> task);
    void resumeTask(uint64_t id, bool mainThread);

    void freePBOs();
    void cancelAll();

    void enqueueTaskGroupPoll(std::shared_ptr<detail::GroupControl> ctl);
};

}
