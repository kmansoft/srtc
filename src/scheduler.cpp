#include "srtc/scheduler.h"
#include "srtc/logging.h"

#include <algorithm>

#include <cassert>

#define LOG(level, tag, ...) srtc::log(level, tag, __VA_ARGS__)

namespace srtc
{

// ----- Task

Task::~Task() = default;

void Task::cancelHelper(std::weak_ptr<Task>& ptr)
{
    if (const auto task = ptr.lock()) {
        task->cancel();
    }
    ptr.reset();
}

// ----- Scheduler

Scheduler::Scheduler() = default;

Scheduler::~Scheduler() = default;

// ----- ThreadScheduler

ThreadScheduler::TaskImpl::TaskImpl(
    const std::weak_ptr<ThreadScheduler>& owner, const When& when, const char* file, int line, const Func& func)
    : mOwner(owner)
    , mWhen(when)
    , mFile(file)
    , mLine(line)
    , mFunc(func)
{
    assert(mOwner.lock());
}

ThreadScheduler::TaskImpl::~TaskImpl() = default;

void ThreadScheduler::TaskImpl::cancel()
{
    if (const auto owner = mOwner.lock()) {
        auto self = shared_from_this();
        owner->cancelImpl(self);
    }
}

std::weak_ptr<Task> ThreadScheduler::TaskImpl::update(const std::chrono::milliseconds& delay)
{
    if (const auto owner = mOwner.lock()) {
        auto self = shared_from_this();
        return owner->updateImpl(self, delay);
    }
    return {};
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

std::weak_ptr<Task> ThreadScheduler::submit(const Delay& delay, const char* file, int line, const Func& func)
{
    // Please instantiate using std::make_shared
    assert(weak_from_this().lock());

    const auto when = std::chrono::steady_clock::now() + delay;
    const auto task = std::make_shared<TaskImpl>(weak_from_this(), when, file, line, func);

    {
        std::lock_guard lock(mMutex);
        mTaskQueue.insert(std::upper_bound(mTaskQueue.begin(), mTaskQueue.end(), task, TaskImplLess()), task);
    }

    mCondVar.notify_one();
    return task;
}

void ThreadScheduler::cancel(std::shared_ptr<Task>& task)
{
    std::shared_ptr impl = std::static_pointer_cast<TaskImpl>(task);
    cancelImpl(impl);
}

std::shared_ptr<RealScheduler> ThreadScheduler::getRealScheduler()
{
    return shared_from_this();
}

void ThreadScheduler::dump()
{
    std::unique_lock lock(mMutex);

    srtc::log(SRTC_LOG_V, "ThreadScheduler", "Queue contains %zd items", mTaskQueue.size());
}

void ThreadScheduler::cancelImpl(const std::shared_ptr<TaskImpl>& task)
{
    std::unique_lock lock(mMutex);

    if (const auto iter = std::find(mTaskQueue.begin(), mTaskQueue.end(), task); iter != mTaskQueue.end()) {
        mTaskQueue.erase(iter);
        return;
    }

    mCondVar.wait(lock, [this, task]() SRTC_EXCLUSIVE_LOCKS_REQUIRED(mMutex) { return mIsQuit || task->mIsCompleted; });
}

std::weak_ptr<Task> ThreadScheduler::updateImpl(const std::shared_ptr<TaskImpl>& oldTask,
                                                const srtc::Scheduler::Delay& delay)
{
    std::unique_lock lock(mMutex);

    if (const auto iter = std::find(mTaskQueue.begin(), mTaskQueue.end(), oldTask); iter != mTaskQueue.end()) {
        mTaskQueue.erase(iter);
    }

    const auto when = std::chrono::steady_clock::now() + delay;
    const auto newTask =
        std::make_shared<TaskImpl>(weak_from_this(), when, oldTask->mFile, oldTask->mLine, oldTask->mFunc);

    mTaskQueue.insert(std::upper_bound(mTaskQueue.begin(), mTaskQueue.end(), newTask, TaskImplLess()), newTask);

    mCondVar.notify_one();
    return newTask;
}

void ThreadScheduler::threadFunc(const std::string name)
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
            const auto delay =
                head == nullptr ? std::chrono::seconds(60) : head->mWhen - std::chrono::steady_clock::now();

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

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety-analysis"
#endif
            if (mIsQuit) {
                break;
            }
#ifdef __clang__
#pragma clang diagnostic pop
#endif

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

// ----- LoopScheduler

LoopScheduler::TaskImpl::TaskImpl(
    const std::weak_ptr<LoopScheduler>& owner, const When& when, const char* file, int line, const Func& func)
    : mOwner(owner)
    , mWhen(when)
    , mFile(file)
    , mLine(line)
    , mFunc(func)
{
    assert(mOwner.lock());
}

LoopScheduler::TaskImpl::~TaskImpl() = default;

void LoopScheduler::TaskImpl::cancel()
{
    if (const auto owner = mOwner.lock()) {
        auto self = shared_from_this();
        owner->cancelImpl(self);
    }
}

std::weak_ptr<Task> LoopScheduler::TaskImpl::update(const Delay& delay)
{
    if (const auto owner = mOwner.lock()) {
        auto self = shared_from_this();
        return owner->updateImpl(self, delay);
    }

    return {};
}

LoopScheduler::LoopScheduler()
    : mThreadId(std::this_thread::get_id())
{
}

LoopScheduler::~LoopScheduler()
{
    assertCurrentThread();
}

std::weak_ptr<Task> LoopScheduler::submit(const Delay& delay, const char* file, int line, const Func& func)
{
    // Please instantiate using std::make_shared
    assert(weak_from_this().lock());

    if (line == 781) {
        LOG(SRTC_LOG_V, "submit for %s %d", file, line);
    }

    assertCurrentThread();

    const auto when = std::chrono::steady_clock::now() + delay;
    const auto task = std::make_shared<TaskImpl>(weak_from_this(), when, file, line, func);

    mTaskQueue.insert(std::upper_bound(mTaskQueue.begin(), mTaskQueue.end(), task, TaskImplLess()), task);

    return task;
}

void LoopScheduler::cancel(std::shared_ptr<Task>& task)
{
    std::shared_ptr impl = std::static_pointer_cast<TaskImpl>(task);
    cancelImpl(impl);
}

std::shared_ptr<RealScheduler> LoopScheduler::getRealScheduler()
{
    return shared_from_this();
}

void LoopScheduler::dump()
{
    srtc::log(SRTC_LOG_V, "LoopScheduler", "Queue contains %zu items", mTaskQueue.size());
}

int LoopScheduler::getTimeoutMillis(int defaultValue) const
{
    assertCurrentThread();

    if (mTaskQueue.empty()) {
        return defaultValue;
    }

    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(mTaskQueue.front()->mWhen -
                                                                            std::chrono::steady_clock::now());
    if (diff <= std::chrono::milliseconds::zero()) {
        return 0;
    }
    if (diff.count() > std::numeric_limits<unsigned int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(diff.count());
}

void LoopScheduler::run()
{
    assertCurrentThread();

    while (!mTaskQueue.empty() && mTaskQueue.front()->mWhen <= std::chrono::steady_clock::now()) {
        const auto task = mTaskQueue.front();
        mTaskQueue.erase(mTaskQueue.begin());
        task->mFunc();
    }
}

void LoopScheduler::assertCurrentThread() const
{
    assert(mThreadId == std::this_thread::get_id());
}

void LoopScheduler::cancelImpl(std::shared_ptr<TaskImpl>& task)
{
    assertCurrentThread();

    if (const auto iter = std::find(mTaskQueue.begin(), mTaskQueue.end(), task); iter != mTaskQueue.end()) {
        mTaskQueue.erase(iter);
    }
}

std::weak_ptr<Task> LoopScheduler::updateImpl(const std::shared_ptr<TaskImpl>& oldTask,
                                              const srtc::Scheduler::Delay& delay)
{
    assertCurrentThread();

    if (const auto iter = std::find(mTaskQueue.begin(), mTaskQueue.end(), oldTask); iter != mTaskQueue.end()) {
        mTaskQueue.erase(iter);
    }

    const auto when = std::chrono::steady_clock::now() + delay;
    const auto newTask =
        std::make_shared<TaskImpl>(weak_from_this(), when, oldTask->mFile, oldTask->mLine, oldTask->mFunc);

    mTaskQueue.insert(std::upper_bound(mTaskQueue.begin(), mTaskQueue.end(), newTask, TaskImplLess()), newTask);

    return newTask;
}

// ScopedScheduler

ScopedScheduler::TaskImpl::TaskImpl(
    ScopedScheduler* owner, const std::weak_ptr<Task>& task, const char* file, int line, const Func& func)
    : mOwner(owner)
    , mTask(task)
    , mFile(file)
    , mLine(line)
    , mFunc(func)
{
}

ScopedScheduler::TaskImpl::~TaskImpl() = default;

void ScopedScheduler::TaskImpl::cancel()
{
    auto self = shared_from_this();
    mOwner->cancelImpl(self);
}

std::weak_ptr<Task> ScopedScheduler::TaskImpl::update(const Delay& delay)
{
    auto self = shared_from_this();
    return mOwner->updateImpl(self, delay);
}

ScopedScheduler::ScopedScheduler(const std::shared_ptr<RealScheduler>& scheduler)
    : mScheduler(scheduler)
{
}

ScopedScheduler::~ScopedScheduler()
{
    std::lock_guard lock(mMutex);

    for (const auto& iter : mSubmitted) {
        if (const auto task = iter->mTask.lock()) {
            task->cancel();
        }
    }

    mSubmitted.clear();
}

std::weak_ptr<Task> ScopedScheduler::submit(const Delay& delay, const char* file, int line, const Func& func)
{
    std::lock_guard lock(mMutex);

    removeExpiredLocked();

    const auto task = mScheduler->submit(delay, file, line, func);
    const auto impl = std::make_shared<TaskImpl>(this, task, file, line, func);
    mSubmitted.push_back(impl);

    return impl;
}

void ScopedScheduler::cancel(std::shared_ptr<Task>& task)
{
    std::shared_ptr impl = std::static_pointer_cast<TaskImpl>(task);
    cancelImpl(impl);
}

std::shared_ptr<RealScheduler> ScopedScheduler::getRealScheduler()
{
    return mScheduler;
}

void ScopedScheduler::cancelImpl(const std::shared_ptr<TaskImpl>& task)
{
    if (const auto ptr = task->mTask.lock()) {
        ptr->cancel();
    }

    std::lock_guard lock(mMutex);

    if (const auto iter = std::find(mSubmitted.begin(), mSubmitted.end(), task); iter != mSubmitted.end()) {
        mSubmitted.erase(iter);
    }
}

std::weak_ptr<Task> ScopedScheduler::updateImpl(const std::shared_ptr<TaskImpl>& oldTask, const Delay& delay)
{
    std::lock_guard lock(mMutex);

    removeExpiredLocked();

    if (const auto iter = std::find(mSubmitted.begin(), mSubmitted.end(), oldTask); iter != mSubmitted.end()) {
        mSubmitted.erase(iter);
    }

    if (const auto ptr = oldTask->mTask.lock()) {
        const auto updated = ptr->update(delay);
        const auto newTask = std::make_shared<TaskImpl>(this, updated, oldTask->mFile, oldTask->mLine, oldTask->mFunc);
        mSubmitted.push_back(newTask);
        return newTask;
    }

    return {};
}

void ScopedScheduler::removeExpiredLocked()
{
    for (auto iter = mSubmitted.begin(); iter != mSubmitted.end();) {
        if ((*iter)->mTask.expired()) {
            iter = mSubmitted.erase(iter);
        } else {
            ++iter;
        }
    }
}

} // namespace srtc
