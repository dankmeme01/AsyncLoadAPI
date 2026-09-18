#pragma once
#include <AsyncLoad/Task.hpp>
#include <AsyncLoad/util/assert.hpp>
#include <asp/time.hpp>
#include <Geode/utils/random.hpp>
#include <Geode/Result.hpp>

namespace AsyncLoad {

namespace detail {
    struct TaskControlBase {
        virtual ~TaskControlBase() = default;
        virtual std::string error() const = 0;

        TaskControlBase() {
            m_id = geode::utils::random::nextU64();
            m_startedAt = asp::Instant::now();
        }

        uint64_t m_id;
        asp::Instant m_startedAt;
        std::string m_error;
        std::string m_name;
        std::vector<geode::Function<void()>> m_completionHooks;
        std::atomic<TaskStatus> m_status{TaskStatus::Pending};
        std::atomic<bool> m_cancelled{false};

        void cancel() {
            m_cancelled.store(true, std::memory_order::release);
            m_status.store(TaskStatus::Cancelled, std::memory_order::release);
        }

        void invokeCompletionHooks() {
            for (auto& hook : m_completionHooks) {
                hook();
            }
            m_completionHooks.clear();
        }

        void addCompletionHook(geode::Function<void()> hook) {
            m_completionHooks.push_back(std::move(hook));
        }


        std::string name() const {
            if (!m_name.empty()) return m_name;
            return fmt::format("Task {}", m_id);
        }
    };

    template <typename T>
    struct TaskControl : TaskControlBase {
        using Callback = geode::Function<void(geode::Result<T>)>;
        explicit TaskControl(Callback callback) : m_callback(std::move(callback)) {}

        void complete(geode::Result<T> result) {
            auto expected = TaskStatus::Pending;
            if (m_status.compare_exchange_strong(expected, result.isOk() ? TaskStatus::Succeeded : TaskStatus::Failed, std::memory_order::release)) {
                m_result = std::move(result);
                if (m_result->isErr()) {
                    m_error = m_result->unwrapErr();
                }
            }
            // if cas failed then the task has been cancelled, no need to store the result
        }

        void invokeCallback() {
            AL_ASSERT(m_result.has_value() && "TaskControl::invokeCallback called before completion");

            if (m_callback) {
                m_callback(std::move(*m_result));
                m_result.reset();
            }
        }

        std::string error() const override {
            return m_error;
        }

        Callback m_callback;
        std::optional<geode::Result<T>> m_result;
    };
}


/// The result of a single operation in a task, returned from advance()
enum class TaskAdvanceResult : uint8_t {
    /// Task is still pending, and advance() should be called again.
    Pending,
    /// Task is still pending, and work needs to be done strictly on main thread.
    /// In this state, advance() will do nothing when in a thread pool, and otherwise will advance the task.
    RequiresMainThread,
    /// Task has finished, either successfully or not. It may also have been cancelled.
    Finished,
};

struct Task {
    virtual ~Task() = default;
    virtual TaskAdvanceResult advance(bool mainThread = false) = 0;
    virtual uint64_t id() const = 0;
    virtual TaskStatus status() const = 0;
    virtual bool cancelled() const = 0;
    virtual void cancel() = 0;
    virtual std::shared_ptr<detail::TaskControlBase> control() const = 0;
    virtual void invokeCallback() = 0;
    virtual void invokeCompletionHooks() = 0;
    /// Adds hook that runs when task is completed/failed/cancelled, not thread safe
    virtual void addCompletionHook(geode::Function<void()> hook) = 0;
    virtual asp::Duration elapsed() const = 0;
    virtual std::string name() const = 0;
    virtual std::string_view typeName() const {
        return "Task";
    }

    bool finished() const {
        return this->status() != TaskStatus::Pending;
    }

    bool pending() const {
        return this->status() == TaskStatus::Pending;
    }
};

template <typename T>
struct TypedTask : Task {
    using Control = detail::TaskControl<T>;
    std::shared_ptr<Control> m_ctl;

    explicit TypedTask(std::shared_ptr<Control> ctl) : m_ctl(std::move(ctl)) {}

    void setStatus(TaskStatus status) {
        m_ctl->m_status.store(status, std::memory_order::release);
    }

    void complete(geode::Result<T> result) {
        m_ctl->complete(std::move(result));
    }

    uint64_t id() const override {
        return m_ctl->m_id;
    }

    TaskStatus status() const override {
        return m_ctl->m_status.load(std::memory_order::acquire);
    }

    /// Returns whether the task is pending AND not cancelled, aka should run
    bool shouldRun() const {
        return this->status() == TaskStatus::Pending;
    }

    bool cancelled() const override {
        return m_ctl->m_cancelled.load(std::memory_order::acquire);
    }

    void cancel() override {
        m_ctl->m_cancelled.store(true, std::memory_order::release);
    }

    std::shared_ptr<detail::TaskControlBase> control() const override {
        return m_ctl;
    }

    void invokeCallback() override {
        m_ctl->invokeCallback();
    }

    void invokeCompletionHooks() override {
        m_ctl->invokeCompletionHooks();
    }

    void addCompletionHook(geode::Function<void()> hook) override {
        m_ctl->addCompletionHook(std::move(hook));
    }

    asp::Duration elapsed() const override {
        return m_ctl->m_startedAt.elapsed();
    }

    std::string name() const override {
        if (!m_ctl->m_name.empty()) return m_ctl->m_name;
        return fmt::format("{} {}", this->typeName(), m_ctl->m_id);
    }
};

}
