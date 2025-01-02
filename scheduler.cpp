#include "srtc/scheduler.h"
#include "srtc/logging.h"

#include <cassert>

#define LOG(...) srtc::log("ThreadScheduler", __VA_ARGS__)

namespace srtc {

// ----- Task

Task::~Task() = default;

// ----- Scheduler

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() = default;

// ----- ThreadScheduler

ThreadScheduler::TaskImpl::TaskImpl(
        const std::weak_ptr<ThreadScheduler>& owner,
        const When& when,
        const Func& func)
    : mOwner(owner)
    , mWhen(when)
    , mFunc(func)
{
}

ThreadScheduler::TaskImpl::~TaskImpl() = default;

void ThreadScheduler::TaskImpl::cancel()
{
    if (const auto owner = mOwner.lock()) {
        auto self = shared_from_this();
        owner->cancelImpl(self);
    }
}

ThreadScheduler::ThreadScheduler(const std::string& name)
    : mThread(&ThreadScheduler::threadFunc, this, name)
{
}

ThreadScheduler::~ThreadScheduler()
{
    std::thread thread;

    {
        std::lock_guard lock(mMutex);
        thread = std::move(mThread);
        mIsQuit = true;
        mCondVar.notify_one();
    }

    if (std::this_thread::get_id() == thread.get_id()) {
        thread.detach();
    } else {
        thread.join();
    }
}

std::weak_ptr<Task> ThreadScheduler::submit(const Delay& delay,
                                            const Func& func)
{
    const auto when = std::chrono::steady_clock::now() + delay;
    const auto task = std::make_shared<TaskImpl>(
            weak_from_this(),
            when,
            func);

    {
        std::lock_guard lock(mMutex);
        mTaskQueue.insert(std::upper_bound(
                mTaskQueue.begin(), mTaskQueue.end(), task, TaskImplLess()), task);

        // Debug
        LOG("Total %zd tasks", mTaskQueue.size());

        const auto now = std::chrono::steady_clock::now();
        for (const auto& iter : mTaskQueue) {
            LOG("Task at %lld millis",
                std::chrono::duration_cast<std::chrono::milliseconds >(iter->mWhen - now).count());
        }
    }

    mCondVar.notify_one();
    return task;
}

void ThreadScheduler::cancel(std::shared_ptr<Task>& task)
{
    std::shared_ptr impl = std::static_pointer_cast<TaskImpl>(task);
    cancelImpl(impl);
}

void ThreadScheduler::cancelImpl(std::shared_ptr<TaskImpl>& task)
{
    std::unique_lock lock(mMutex);

    const auto iter = std::find(mTaskQueue.begin(), mTaskQueue.end(), task);
    if (iter != mTaskQueue.end()) {
        mTaskQueue.erase(iter);
        return;
    }

    mCondVar.wait(lock,  [this, task]() SRTC_EXCLUSIVE_LOCKS_REQUIRED(mMutex)   {
       return mIsQuit || task->mIsCompleted;
    });
}

void ThreadScheduler::threadFunc(std::string name)
{
#ifdef _POSIX_VERSION
    pthread_setname_np(pthread_self(), name.c_str());
#endif

    std::shared_ptr<TaskImpl> task;

    while (true) {

        {
            std::unique_lock lock(mMutex);

            if (task) {
                task->mIsCompleted = true;
                task.reset();
                mCondVar.notify_all();
            }

            const auto head = mTaskQueue.empty() ? nullptr : mTaskQueue.front();
            const auto delay = head == nullptr ? std::chrono::seconds(60) : head->mWhen - std::chrono::steady_clock::now();

            if (delay.count() <= 0) {
                task = head;
            } else {
                mCondVar.wait_for(lock, delay, [this, head = head.get()]() SRTC_EXCLUSIVE_LOCKS_REQUIRED(mMutex) {
                    if (mIsQuit) {
                        return true;
                    }
                    const auto curr = mTaskQueue.empty() ? nullptr : mTaskQueue.front();
                    if (curr.get() != head) {
                        return true;
                    }
                    return curr && curr->mWhen <= std::chrono::steady_clock::now();
                });
            }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
            if (mIsQuit) {
                break;
            }
#pragma clang diagnostic pop

            if (!mTaskQueue.empty()) {
                if (mTaskQueue.front()->mWhen <= std::chrono::steady_clock::now()) {
                    task = mTaskQueue.front();
                    mTaskQueue.erase(mTaskQueue.begin());
                }
            }
        }

        if (task) {
            task->mFunc();
        }
    }
}

}
