#pragma once

#include <chrono>
#include <atomic>
#include <functional>
#include <thread>
#include <memory>
#include <thread>

#include "srtc/srtc.h"

namespace srtc {

// ----- Task

class Task {
public:
    virtual void cancel() = 0;

    virtual ~Task();
};

// ----- Scheduler

class Scheduler {
public:
    Scheduler();

    virtual ~Scheduler();

    using Delay = std::chrono::milliseconds;
    using Func = std::function<void(void)>;

    std::weak_ptr<Task> submit(const Func& func)
    {
        return submit(Delay{0}, func);
    }

    virtual std::weak_ptr<Task> submit(const Delay& delay,
                                       const Func& func) = 0;

    virtual void cancel(std::shared_ptr<Task>& task) = 0;

protected:
    using When = std::chrono::steady_clock::time_point;
};

// ----- ThreadScheduler

class ThreadScheduler final :
        public Scheduler,
        private std::enable_shared_from_this<ThreadScheduler> {
public:
    explicit ThreadScheduler(const std::string &name);

    ~ThreadScheduler() override;

    std::weak_ptr<Task> submit(const Delay& delay,
                               const Func& func) override;

    void cancel(std::shared_ptr<Task>& task) override;

private:

    class TaskImpl final : public Task, private std::enable_shared_from_this<TaskImpl> {
    public:
        TaskImpl(const std::weak_ptr<ThreadScheduler>& owner,
                 const When& when,
                 const Func& func);

        ~TaskImpl() override;

        void cancel() override;

        const std::weak_ptr<ThreadScheduler> mOwner;
        const When mWhen;
        const Func mFunc;

        bool mIsCompleted = {false};
    };

    struct TaskImplLess {
        bool operator()(
                const std::shared_ptr<TaskImpl>& left,
                const std::shared_ptr<TaskImpl>& right)
        {
            return left->mWhen < right->mWhen;
        };
    };

    void cancelImpl(std::shared_ptr<TaskImpl> &task);

    void threadFunc(std::string name);

    std::mutex mMutex;
    std::condition_variable mCondVar;

    std::thread mThread SRTC_GUARDED_BY(mMutex);
    bool mIsQuit SRTC_GUARDED_BY(mMutex) = {false};

    std::vector<std::shared_ptr<TaskImpl>> mTaskQueue;
};

// ----- LoopScheduler

class LoopScheduler final :
        public Scheduler,
        private std::enable_shared_from_this<LoopScheduler> {
public:
    LoopScheduler();

    ~LoopScheduler() override;

    std::weak_ptr<Task> submit(const Delay& delay,
                               const Func& func) override;

    void cancel(std::shared_ptr<Task>& task) override;

    [[nodiscard]] int getTimeoutMillis() const;

    void run();

private:

    class TaskImpl final : public Task, private std::enable_shared_from_this<TaskImpl> {
    public:
        TaskImpl(const std::weak_ptr<LoopScheduler>& owner,
                 const When& when,
                 const Func& func);

        ~TaskImpl() override;

        void cancel() override;

        const std::weak_ptr<LoopScheduler> mOwner;
        const When mWhen;
        const Func mFunc;
    };

    struct TaskImplLess {
        bool operator()(
                const std::shared_ptr<TaskImpl>& left,
                const std::shared_ptr<TaskImpl>& right)
        {
            return left->mWhen < right->mWhen;
        };
    };

    void assertCurrentThread() const;

    void cancelImpl(std::shared_ptr<TaskImpl> &task);

    std::thread::id mThreadId;
    std::vector<std::shared_ptr<TaskImpl>> mTaskQueue;
};

// ----- ScopedScheduler

class ScopedScheduler final :
        public Scheduler,
        private std::enable_shared_from_this<ScopedScheduler> {
public:
    explicit ScopedScheduler(const std::shared_ptr<Scheduler>& scheduler);

    ~ScopedScheduler() override;

    std::weak_ptr<Task> submit(const Delay& delay,
                               const Func& func) override;

    void cancel(std::shared_ptr<Task>& task) override;

private:
    class TaskImpl final : public Task, private std::enable_shared_from_this<TaskImpl> {
    public:
        TaskImpl(const std::weak_ptr<ScopedScheduler>& owner,
                 const std::weak_ptr<Task>& task);

        ~TaskImpl() override;

        void cancel() override;

        const std::weak_ptr<ScopedScheduler> mOwner;
        const std::weak_ptr<Task> mTask;
    };

    void cancelImpl(std::shared_ptr<TaskImpl> &task);

    std::vector<std::shared_ptr<TaskImpl>> mSubmitted SRTC_GUARDED_BY(mMutex);
    std::mutex mMutex;

    const std::shared_ptr<Scheduler> mScheduler;
};

}
