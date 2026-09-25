/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#if STLAB_TASK_SYSTEM(LIBDISPATCH)
#include <stlab/concurrency/detail/libdispatch_executor_group.hpp>
#endif

#if STLAB_TASK_SYSTEM(PORTABLE)
#include <condition_variable>
#include <memory>
#include <stlab/concurrency/set_current_thread_name.hpp>
#endif

#include <stlab/pre_exit.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if STLAB_TASK_SYSTEM(WINDOWS)
#include <Windows.h> // NOLINT(misc-include-cleaner)
#include <new>
#endif

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// Bundles the raw components needed to relocate a task's target across the executor ABI, without
/// constructing an intermediate `task<void() noexcept>`.
struct task_relocation {
    const task<void() noexcept>::concept_t* vtable;
    task<void() noexcept>::invoke_t invoke;
    void* source;
};

namespace {

/// Maps an executor priority to its queue index.
constexpr auto executor_priority_index(executor_priority priority) -> std::size_t {
    switch (priority) {
        case executor_priority::high:
            return 0;
        case executor_priority::medium:
            return 1;
        case executor_priority::low:
            return 2;
    }

    assert(false && "Unknown executor priority.");
    return 1;
}

/// Synchronizes access to a FIFO shard of executor tasks.
class task_shard {
    using task_t = task<void() noexcept>;

    std::mutex _mutex;
    std::deque<task_t> _tasks;

public:
    /// Attempts to remove and return the oldest task without blocking.
    ///
    /// - Postcondition: returns an empty task if the shard is locked or empty.
    auto try_pop() -> task_t {
        std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock};
        if (!lock || _tasks.empty()) return nullptr;

        auto result = std::move(_tasks.front());
        _tasks.pop_front();
        return result;
    }

    /// Removes and returns the oldest task, waiting only for the shard lock.
    ///
    /// - Postcondition: returns an empty task if the shard is empty.
    auto pop() -> task_t {
        std::unique_lock<std::mutex> lock{_mutex};
        if (_tasks.empty()) return nullptr;

        auto result = std::move(_tasks.front());
        _tasks.pop_front();
        return result;
    }

    /// Attempts to append a task without blocking.
    ///
    /// - Postcondition: returns `true` if the task was appended; otherwise returns `false`.
    auto try_push(task_relocation r) -> bool {
        std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock};
        if (!lock) return false;

        _tasks.emplace_back(r.vtable, r.invoke, r.source);
        return true;
    }

    /// Appends a task, waiting until the shard is available.
    void push(task_relocation r) {
        std::unique_lock<std::mutex> lock{_mutex};
        _tasks.emplace_back(r.vtable, r.invoke, r.source);
    }
};

/// Distributes executor tasks across mutex-protected task shards.
class sharded_task_queue {
    using task_t = task<void() noexcept>;

private:
    std::vector<task_shard> _shards;
    std::atomic<unsigned> _index{0};

public:
    /// Constructs a queue with `shard_count` shards.
    ///
    /// - Precondition: `shard_count` is non-zero before calling `enqueue()`.
    explicit sharded_task_queue(unsigned shard_count) : _shards(shard_count) {}

    /// Enqueues a task and returns the selected shard.
    ///
    /// - Precondition: `r.vtable` and `r.invoke` are not `nullptr`.
    /// - Complexity: O(number of shards) in the contended case.
    auto enqueue(task_relocation r) -> std::size_t {
        assert(!_shards.empty() && "Executor must have at least one shard.");

        const auto index = _index.fetch_add(1, std::memory_order_relaxed);
        for (unsigned n = 0; n != _shards.size(); ++n) {
            const auto shard = (index + n) % _shards.size();
            if (_shards[shard].try_push(r)) return shard;
        }

        const auto shard = index % _shards.size();
        _shards[shard].push(r);
        return shard;
    }

    /// Attempts to remove one task, beginning at `hint`.
    ///
    /// - Postcondition: returns an empty task if no shard can be locked or is non-empty.
    /// - Complexity: O(number of shards).
    auto try_pop(std::size_t hint) -> task_t {
        if (_shards.empty()) return nullptr;

        for (std::size_t n = 0; n != _shards.size(); ++n) {
            const auto shard = (hint + n) % _shards.size();
            if (auto task = _shards[shard].try_pop()) return task;
        }

        return nullptr;
    }

    /// Removes and returns one task from the hinted shard, waiting only for that shard's lock.
    ///
    /// - Postcondition: returns an empty task if the hinted shard is empty.
    auto pop(std::size_t hint) -> task_t {
        if (_shards.empty()) return nullptr;

        return _shards[hint % _shards.size()].pop();
    }
};

/// Returns the configured hardware concurrency, clamped to the task-pool limit.
auto portable_hardware_concurrency() -> unsigned {
#if STLAB_TASK_POOL_MAXIMUM() > 0
    return std::clamp(STLAB_TASK_POOL_MAXIMUM(), 1u, std::thread::hardware_concurrency());
#else
    return std::max(1u, std::thread::hardware_concurrency());
#endif
}

/// Returns the number of task shards used by the executor.
auto executor_shard_count() -> unsigned {
    return std::max(1u, portable_hardware_concurrency() - 1);
}

/// Owns the process-shared task queues for all executor priorities.
class shared_executor_queues {
public:
private:
    std::array<sharded_task_queue, 3> _queues{sharded_task_queue{executor_shard_count()},
                                              sharded_task_queue{executor_shard_count()},
                                              sharded_task_queue{executor_shard_count()}};

public:
    /// Enqueues a task at the requested priority.
    auto submit(executor_priority priority, task_relocation r) -> std::size_t {
        return _queues[executor_priority_index(priority)].enqueue(r);
    }

    /// Attempts to remove one task at the requested priority.
    auto try_pop(executor_priority priority, std::size_t hint) -> task<void() noexcept> {
        return _queues[executor_priority_index(priority)].try_pop(hint);
    }

    /// Removes one task from the hinted shard at the requested priority.
    auto pop(executor_priority priority, std::size_t hint) -> task<void() noexcept> {
        return _queues[executor_priority_index(priority)].pop(hint);
    }
};

/// Returns the process-shared executor queues.
auto executor_queues() -> shared_executor_queues& {
    static shared_executor_queues queues;
    return queues;
}

/// Encodes a shard hint for transport through a platform callback context.
auto pack_hint(std::size_t hint) -> void* {
    assert(hint <= static_cast<std::size_t>(UINTPTR_MAX));
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(hint));
}

/// Decodes a shard hint transported through a platform callback context.
auto unpack_hint(void* context) -> std::size_t {
    return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(context));
}

/// Runs one task for `Priority`, rescheduling when contention prevents a pop.
///
/// - Precondition: each invocation corresponds to exactly one queued task.
template <executor_priority Priority, class Reschedule>
void run_wake(std::size_t hint, Reschedule&& reschedule) {
    if (auto task = executor_queues().try_pop(Priority, hint)) {
        task();
        return;
    }

    std::forward<Reschedule>(reschedule)(hint);
}

/// Queues a task and schedules its wake-up.
///
/// - Precondition: `schedule` can submit the wake-up callback for the selected shard.
template <class Schedule>
void submit_and_schedule(executor_priority priority, task_relocation r, Schedule&& schedule) {
    auto submission = executor_queues().submit(priority, r);
    std::forward<Schedule>(schedule)(submission);
}

} // namespace

#if STLAB_TASK_SYSTEM(LIBDISPATCH)

namespace {

/// Maps an executor priority to its libdispatch queue priority.
constexpr auto platform_priority(executor_priority priority) {
    switch (priority) {
        case executor_priority::high:
            return DISPATCH_QUEUE_PRIORITY_HIGH;
        case executor_priority::medium:
            return DISPATCH_QUEUE_PRIORITY_DEFAULT;
        case executor_priority::low:
            return DISPATCH_QUEUE_PRIORITY_LOW;
    }

    assert(false && "Unknown executor priority.");
    return DISPATCH_QUEUE_PRIORITY_DEFAULT;
}

static_assert(platform_priority(executor_priority::high) == DISPATCH_QUEUE_PRIORITY_HIGH);
static_assert(platform_priority(executor_priority::medium) == DISPATCH_QUEUE_PRIORITY_DEFAULT);
static_assert(platform_priority(executor_priority::low) == DISPATCH_QUEUE_PRIORITY_LOW);

/// Schedules a platform wake-up for a priority queue.
template <executor_priority Priority>
void schedule_dispatch_wake(std::size_t hint);

/// Dispatches one queued task and reschedules remaining work.
template <executor_priority Priority>
void dispatch_wake(void* context) {
    run_wake<Priority>(unpack_hint(context),
                       [](std::size_t next_hint) { schedule_dispatch_wake<Priority>(next_hint); });
}

/// Schedules a dispatch callback carrying a shard hint.
template <executor_priority Priority>
void schedule_dispatch_wake(std::size_t hint) {
    dispatch_group_async_f(group()._group,
                           dispatch_get_global_queue(platform_priority(Priority), 0),
                           pack_hint(hint), &dispatch_wake<Priority>);
}

} // namespace

/// Returns the dispatch group used by the executor.
auto group() -> const group_t& {
    static const group_t g = [] {
        group_t result;
        at_pre_exit([]() noexcept { dispatch_group_wait(group()._group, DISPATCH_TIME_FOREVER); });
        return result;
    }();

    return g;
}

/// Waits for dispatch callbacks and releases the dispatch group.
group_t::~group_t() {
    if (_group) {
        dispatch_group_wait(_group, DISPATCH_TIME_FOREVER);
#if !STLAB_FEATURE(OBJC_ARC)
        dispatch_release(_group);
#endif
    }
}

/// Submits one task to the platform executor at the requested priority.
void submit_executor_task(executor_priority priority, task_relocation r) {
    switch (priority) {
        case executor_priority::high:
            submit_and_schedule(priority, r, [](std::size_t hint) {
                schedule_dispatch_wake<executor_priority::high>(hint);
            });
            break;
        case executor_priority::medium:
            submit_and_schedule(priority, r, [](std::size_t hint) {
                schedule_dispatch_wake<executor_priority::medium>(hint);
            });
            break;
        case executor_priority::low:
            submit_and_schedule(priority, r, [](std::size_t hint) {
                schedule_dispatch_wake<executor_priority::low>(hint);
            });
            break;
    }
}

#elif STLAB_TASK_SYSTEM(WINDOWS)

// Windows thread-pool declarations are provided through the Windows.h umbrella header.
// NOLINTBEGIN(misc-include-cleaner)
namespace {

/// Maps an executor priority to its Windows thread-pool callback priority.
constexpr auto platform_priority(executor_priority priority) {
    switch (priority) {
        case executor_priority::high:
            return TP_CALLBACK_PRIORITY_HIGH;
        case executor_priority::medium:
            return TP_CALLBACK_PRIORITY_NORMAL;
        case executor_priority::low:
            return TP_CALLBACK_PRIORITY_LOW;
    }

    assert(false && "Unknown executor priority.");
    return TP_CALLBACK_PRIORITY_NORMAL;
}

/// Owns the Windows thread-pool wake-up resources for one priority.
template <executor_priority Priority>
class windows_wake_system;

/// Returns the Windows wake-up system for one priority.
template <executor_priority Priority>
auto wake_system() -> windows_wake_system<Priority>&;

/// Owns a Windows thread pool and its cleanup group.
template <executor_priority Priority>
class windows_wake_system {
    PTP_POOL _pool = nullptr;
    TP_CALLBACK_ENVIRON _callback_environment{};
    PTP_CLEANUP_GROUP _cleanup_group = nullptr;

public:
    /// Creates the Windows thread-pool resources.
    windows_wake_system() {
        InitializeThreadpoolEnvironment(&_callback_environment);

        _pool = CreateThreadpool(nullptr);
        if (_pool == nullptr) throw std::bad_alloc{};

        _cleanup_group = CreateThreadpoolCleanupGroup();
        if (_cleanup_group == nullptr) {
            CloseThreadpool(_pool);
            throw std::bad_alloc{};
        }

        SetThreadpoolCallbackPriority(&_callback_environment, platform_priority(Priority));
        SetThreadpoolCallbackPool(&_callback_environment, _pool);
        SetThreadpoolCallbackCleanupGroup(&_callback_environment, _cleanup_group, nullptr);
    }

    /// Disables copying of the thread-pool owner.
    windows_wake_system(const windows_wake_system&) = delete;

    /// Disables assignment of the thread-pool owner.
    auto operator=(const windows_wake_system&) -> windows_wake_system& = delete;

    /// Destroys an already-joined Windows wake-up system.
    ~windows_wake_system() {
        assert(_pool == nullptr && "stlab: Thread pool not joined prior to destruction.");
    }

    /// Schedules one platform callback carrying a shard hint.
    void schedule(std::size_t hint) {
        auto work = CreateThreadpoolWork(&callback, pack_hint(hint), &_callback_environment);
        assert(work != nullptr && "CreateThreadpoolWork failed.");

        SubmitThreadpoolWork(work);
    }

    /// Joins pending callbacks and releases all Windows resources.
    void join() {
        CloseThreadpoolCleanupGroupMembers(_cleanup_group, FALSE, nullptr);
        CloseThreadpoolCleanupGroup(_cleanup_group);
        CloseThreadpool(_pool);
        DestroyThreadpoolEnvironment(&_callback_environment);
        _cleanup_group = nullptr;
        _pool = nullptr;
    }

private:
    /// Runs one queued task and schedules remaining work.
    static void CALLBACK callback(PTP_CALLBACK_INSTANCE /*instance*/,
                                  PVOID parameter,
                                  PTP_WORK work) {
        run_wake<Priority>(unpack_hint(parameter), [](std::size_t next_hint) {
            wake_system<Priority>().schedule(next_hint);
        });
        CloseThreadpoolWork(work);
    }
};

/// Returns and initializes the Windows wake-up system for one priority.
template <executor_priority Priority>
auto wake_system() -> windows_wake_system<Priority>& {
    static windows_wake_system<Priority> result;
    static const auto registered = [] {
        at_pre_exit([]() noexcept { wake_system<Priority>().join(); });
        return true;
    }();
    (void)registered;
    return result;
}

} // namespace

/// Submits one task to the Windows executor at the requested priority.
void submit_executor_task(executor_priority priority, task_relocation r) {
    switch (priority) {
        case executor_priority::high:
            submit_and_schedule(priority, r, [](std::size_t hint) {
                wake_system<executor_priority::high>().schedule(hint);
            });
            break;
        case executor_priority::medium:
            submit_and_schedule(priority, r, [](std::size_t hint) {
                wake_system<executor_priority::medium>().schedule(hint);
            });
            break;
        case executor_priority::low:
            submit_and_schedule(priority, r, [](std::size_t hint) {
                wake_system<executor_priority::low>().schedule(hint);
            });
            break;
    }
}
// NOLINTEND(misc-include-cleaner)

#elif STLAB_TASK_SYSTEM(PORTABLE)

/// Owns the process-shared portable task system.
class priority_task_system {
    struct implementation;
    std::unique_ptr<implementation> _impl;

public:
    /// Constructs the portable task system and starts its initial workers.
    priority_task_system();

    /// Disables copying of the portable task system.
    priority_task_system(const priority_task_system&) = delete;

    /// Disables assignment of the portable task system.
    auto operator=(const priority_task_system&) -> priority_task_system& = delete;

    /// Disables moving of the portable task system.
    priority_task_system(priority_task_system&&) = delete;

    /// Disables move-assignment of the portable task system.
    auto operator=(priority_task_system&&) -> priority_task_system& = delete;

    /// Destroys the portable task system.
    ~priority_task_system();

    /// Submits one task to the shared portable executor state.
    ///
    /// - Precondition: `r.vtable` and `r.invoke` are not `nullptr`.
    /// - Postcondition: exactly one execution of the relocated target is scheduled.
    void submit(executor_priority priority, task_relocation r);

    /// Wakes one waiting worker if one is available.
    auto wake() -> bool;

    /// Adds one expansion worker when the pool may otherwise stall.
    void add_thread();

    /// Joins all worker threads after shared executor shutdown begins.
    void join();
};

/// Coordinates one portable executor worker's sleep, wake, and shutdown state.
class waiter {
    std::mutex _mutex;
    std::condition_variable _ready;
    bool _waiting{false};
    bool _done{false};

public:
    /// Signals this worker to terminate.
    void done() {
        {
            std::unique_lock<std::mutex> lock{_mutex};
            _done = true;
        }
        _ready.notify_one();
    }

    /// Attempts to wake this worker.
    ///
    /// - Postcondition: returns `true` only when this worker was waiting and was signaled.
    auto wake() -> bool {
        {
            std::unique_lock<std::mutex> lock{_mutex};
            if (!_waiting) return false;
            _waiting = false;
        }
        _ready.notify_one();
        return true;
    }

    /// Waits for work, an explicit wake, or shutdown.
    ///
    /// - Postcondition: returns `true` when shutdown was requested.
    auto wait() -> bool {
        std::unique_lock<std::mutex> lock{_mutex};
        _waiting = true;
        while (_waiting && !_done)
            _ready.wait(lock);
        _waiting = false;
        return _done;
    }
};

/// Implements the portable priority task system's worker pool.
struct priority_task_system::implementation {
    const unsigned _worker_count{executor_shard_count()};
    const unsigned _thread_limit{std::max(9U, portable_hardware_concurrency() * 4 + 1)};

    std::mutex _mutex;
    std::vector<std::thread> _threads;
    std::vector<waiter> _waiters{_thread_limit};

    /// Starts the initial portable executor workers.
    implementation() {
        _threads.reserve(_thread_limit);
        for (unsigned i = 0; i != _worker_count; ++i)
            add_thread_unlocked(i);
    }

    /// Queues a task and wakes the worker responsible for the selected shard.
    void submit(executor_priority priority, task_relocation r) {
        const auto shard = executor_queues().submit(priority, r);
        (void)_waiters[shard].wake();
    }

    /// Attempts to wake one waiting worker.
    auto wake() -> bool {
        for (auto& waiter : _waiters) {
            if (waiter.wake()) return true;
        }
        return false;
    }

    /// Adds an expansion worker unless the thread limit has been reached.
    void add_thread() {
        std::unique_lock<std::mutex> lock{_mutex};
        if (_threads.size() == _thread_limit) return;
        add_thread_unlocked(_threads.size());
    }

    /// Signals and joins all workers.
    void join() {
        for (auto& waiter : _waiters)
            waiter.done();
        for (auto& thread : _threads)
            thread.join();
        _threads.clear();
    }

private:
    /// Starts a worker with the specified queue hint.
    void add_thread_unlocked(std::size_t index) {
        _threads.emplace_back([this, index] {
            const auto name = index < _worker_count ? "cc.stlab.default_executor" :
                                                      "cc.stlab.default_executor.expansion";
            stlab::set_current_thread_name(name);

            while (true) {
                if (auto task = try_pop(index)) {
                    task();
                    continue;
                }

                if (auto task = pop(index)) {
                    task();
                    continue;
                }

                if (_waiters[index].wait()) return;
            }
        });
    }

    /// Attempts to remove one task, honoring priority order.
    auto try_pop(std::size_t hint) -> task<void() noexcept> {
        if (auto task = executor_queues().try_pop(executor_priority::high, hint)) return task;
        if (auto task = executor_queues().try_pop(executor_priority::medium, hint)) return task;
        return executor_queues().try_pop(executor_priority::low, hint);
    }

    /// Removes one task from the hinted shard, honoring priority order.
    auto pop(std::size_t hint) -> task<void() noexcept> {
        if (auto task = executor_queues().pop(executor_priority::high, hint)) return task;
        if (auto task = executor_queues().pop(executor_priority::medium, hint)) return task;
        return executor_queues().pop(executor_priority::low, hint);
    }
};

/// Constructs the portable priority task system.
priority_task_system::priority_task_system() : _impl(std::make_unique<implementation>()) {}

/// Destroys the portable priority task system.
priority_task_system::~priority_task_system() = default;

/// Submits a task to the portable priority task system.
void priority_task_system::submit(executor_priority priority, task_relocation r) {
    _impl->submit(priority, r);
}

/// Attempts to wake one portable executor worker.
auto priority_task_system::wake() -> bool { return _impl->wake(); }

/// Adds an expansion worker to the portable task system.
void priority_task_system::add_thread() { _impl->add_thread(); }

/// Signals and joins all portable executor workers.
void priority_task_system::join() { _impl->join(); }

/// Returns the process-shared portable task system.
auto pts() -> priority_task_system& {
    static priority_task_system only_task_system;
    static const auto registered = [] {
        at_pre_exit([]() noexcept { pts().join(); });
        return true;
    }();
    (void)registered;
    return only_task_system;
}

/// Submits a task to the process-shared portable executor.
void submit_executor_task(executor_priority priority, task_relocation r) {
    pts().submit(priority, r);
}

#endif

} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {
/// Notifies the shared default executor that the calling thread is about to wait.
extern "C" void stlab_v2_notify_default_executor_before_waiting() noexcept {
#if STLAB_TASK_SYSTEM(PORTABLE)
    if (!STLAB_VERSION_NAMESPACE()::detail::pts().wake())
        STLAB_VERSION_NAMESPACE()::detail::pts().add_thread();
#endif
}

/// Submits one task to the shared default-priority executor.
extern "C" void stlab_v2_default_executor_submit(const stlab_v2_task_concept_t* vtable,
                                                 stlab_v2_task_proc invoke,
                                                 void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    STLAB_VERSION_NAMESPACE()::detail::submit_executor_task(
        STLAB_VERSION_NAMESPACE()::detail::executor_priority::medium,
        STLAB_VERSION_NAMESPACE()::detail::task_relocation{vtable, invoke, source});
}

/// Submits one task to the shared high-priority executor.
extern "C" void stlab_v2_high_executor_submit(const stlab_v2_task_concept_t* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    STLAB_VERSION_NAMESPACE()::detail::submit_executor_task(
        STLAB_VERSION_NAMESPACE()::detail::executor_priority::high,
        STLAB_VERSION_NAMESPACE()::detail::task_relocation{vtable, invoke, source});
}

/// Submits one task to the shared low-priority executor.
extern "C" void stlab_v2_low_executor_submit(const stlab_v2_task_concept_t* vtable,
                                             stlab_v2_task_proc invoke,
                                             void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    STLAB_VERSION_NAMESPACE()::detail::submit_executor_task(
        STLAB_VERSION_NAMESPACE()::detail::executor_priority::low,
        STLAB_VERSION_NAMESPACE()::detail::task_relocation{vtable, invoke, source});
}

} // namespace v2
} // namespace stlab
