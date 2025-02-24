#pragma once

#include <chrono>
#include <atomic>
#include <functional>
#include <thread>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <list>

#include "srtc/srtc.h"

namespace srtc {

class RealScheduler;

// ----- Task

class Task {
public:
    virtual void cancel() = 0;
    virtual std::weak_ptr<Task> update(const std::chrono::milliseconds& delay) = 0;

    virtual ~Task();
};

// ----- Scheduler

class Scheduler {
public:
    Scheduler();

    virtual ~Scheduler();

    using Delay = std::chrono::milliseconds;
    using Func = std::function<void(void)>;

    std::weak_ptr<Task> submit(const char* file,
                               int line,
                               const Func& func)
    {
        return submit(Delay{0}, file, line, func);
    }

    virtual std::weak_ptr<Task> submit(const Delay& delay,
                                       const char* file,
                                       int line,
                                       const Func& func) = 0;

    virtual void cancel(std::shared_ptr<Task>& task) = 0;

    [[nodiscard]] virtual std::shared_ptr<RealScheduler> getRealScheduler() = 0;

protected:
    using When = std::chrono::steady_clock::time_point;
};

// ---- RealScheduler

class RealScheduler : public Scheduler {
public:
    RealScheduler() = default;
    ~RealScheduler() override = default;

    virtual void dump() = 0;
};


// ----- ThreadScheduler

class ThreadScheduler final :
        public RealScheduler,
        public std::enable_shared_from_this<ThreadScheduler> {
public:
    explicit ThreadScheduler(const std::string &name);

    ~ThreadScheduler() override;

    std::weak_ptr<Task> submit(const Delay& delay,
                               const char* file,
                               int line,
                               const Func& func) override;

    void cancel(std::shared_ptr<Task>& task) override;

    std::shared_ptr<RealScheduler> getRealScheduler() override;

    void dump() override;

private:

    class TaskImpl final : public Task, public std::enable_shared_from_this<TaskImpl> {
    public:
        TaskImpl(const std::weak_ptr<ThreadScheduler>& owner,
                 const When& when,
                 const char* file,
                 int line,
                 const Func& func);

        ~TaskImpl() override;

        void cancel() override;
        std::weak_ptr<Task> update(const Delay& delay) override;

        const std::weak_ptr<ThreadScheduler> mOwner;
        const When mWhen;
        const char* const mFile;
        const int mLine;
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

    void cancelImpl(const std::shared_ptr<TaskImpl>& task);
    std::weak_ptr<Task> updateImpl(const std::shared_ptr<TaskImpl>& oldTask,
                                   const Delay& delay);

    void threadFunc(std::string name);

    std::mutex mMutex;
    std::condition_variable mCondVar;

    std::thread mThread SRTC_GUARDED_BY(mMutex);
    bool mIsQuit SRTC_GUARDED_BY(mMutex) = {false};

    std::vector<std::shared_ptr<TaskImpl>> mTaskQueue;
};

// ----- LoopScheduler

class LoopScheduler final :
        public RealScheduler,
        public std::enable_shared_from_this<LoopScheduler> {
public:
    LoopScheduler();

    ~LoopScheduler() override;

    std::weak_ptr<Task> submit(const Delay& delay,
                               const char* file,
                               int line,
                               const Func& func) override;

    void cancel(std::shared_ptr<Task>& task) override;

    std::shared_ptr<RealScheduler> getRealScheduler() override;

    void dump() override;

    [[nodiscard]] int getTimeoutMillis(int defaultValue) const;

    void run();

private:

    class TaskImpl final : public Task, public std::enable_shared_from_this<TaskImpl> {
    public:
        TaskImpl(const std::weak_ptr<LoopScheduler>& owner,
                 const When& when,
                 const char* file,
                 int line,
                 const Func& func);

        ~TaskImpl() override;

        void cancel() override;
        std::weak_ptr<Task> update(const Delay& delay) override;

        const std::weak_ptr<LoopScheduler> mOwner;
        const When mWhen;
        const char* const mFile;
        int mLine;
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
    std::weak_ptr<Task> updateImpl(const std::shared_ptr<TaskImpl>& oldTask,
                                   const Delay& delay);

    std::thread::id mThreadId;
    std::vector<std::shared_ptr<TaskImpl>> mTaskQueue;
};

// ----- ScopedScheduler

class ScopedScheduler final :
        public Scheduler {
public:
    explicit ScopedScheduler(const std::shared_ptr<RealScheduler>& scheduler);

    ~ScopedScheduler() override;

    std::weak_ptr<Task> submit(const Delay& delay,
                               const char* file,
                               int line,
                               const Func& func) override;

    void cancel(std::shared_ptr<Task>& task) override;

    [[nodiscard]] std::shared_ptr<RealScheduler> getRealScheduler() override;

private:
    class TaskImpl final : public Task, public std::enable_shared_from_this<TaskImpl> {
    public:
        TaskImpl(ScopedScheduler* owner,
                 const std::weak_ptr<Task>& task,
                 const char* file,
                 int line,
                 const Func& func);

        ~TaskImpl() override;

        void cancel() override;
        std::weak_ptr<Task> update(const Delay& delay) override;

        ScopedScheduler* const mOwner;
        const std::weak_ptr<Task> mTask;
        const char* const mFile;
        const int mLine;
        const Func mFunc;
    };

    void cancelImpl(const std::shared_ptr<TaskImpl>& task);
    std::weak_ptr<Task> updateImpl(const std::shared_ptr<TaskImpl>& oldTask,
                                   const Delay& delay);

    void removeExpiredLocked() SRTC_SHARED_LOCKS_REQUIRED(mMutex);

    std::list<std::shared_ptr<TaskImpl>> mSubmitted SRTC_GUARDED_BY(mMutex);
    std::mutex mMutex;

    const std::shared_ptr<RealScheduler> mScheduler;
};

}
