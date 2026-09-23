/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <stlab/concurrency/default_executor.hpp>

#if STLAB_TASK_SYSTEM(PORTABLE)
#include <stlab/concurrency/set_current_thread_name.hpp>
#endif

#include <stlab/pre_exit.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#if STLAB_TASK_SYSTEM(WINDOWS)
#include <Windows.h>
#endif

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

namespace {

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

class task_shard {
    using task_t = task<void() noexcept>;
    struct element_t {
        std::size_t _id;
        task_t _task;
    };

    std::mutex _mutex;
    std::deque<element_t> _tasks;

public:
    auto try_pop() -> task_t {
        std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock};
        if (!lock || _tasks.empty()) return nullptr;

        auto result = std::move(_tasks.front()._task);
        _tasks.pop_front();
        return result;
    }

    template <class F>
    auto try_push(std::size_t id, F&& f) -> bool {
        std::unique_lock<std::mutex> lock{_mutex, std::try_to_lock};
        if (!lock) return false;

        _tasks.emplace_back(element_t{id, std::forward<F>(f)});
        return true;
    }

    template <class F>
    void push(std::size_t id, F&& f) {
        std::unique_lock<std::mutex> lock{_mutex};
        _tasks.emplace_back(element_t{id, std::forward<F>(f)});
    }

    auto reclaim(std::size_t id) -> task_t {
        std::unique_lock<std::mutex> lock{_mutex};
        auto found = std::find_if(begin(_tasks), end(_tasks),
                                  [=](const auto& element) { return element._id == id; });
        if (found == end(_tasks)) return nullptr;

        auto result = std::move(found->_task);
        _tasks.erase(found);
        return result;
    }
};

class sharded_task_queue {
    using task_t = task<void() noexcept>;
public:
    struct enqueue_result_t {
        std::size_t _shard;
        std::size_t _id;
    };

private:
    std::vector<task_shard> _shards;
    std::atomic<unsigned> _index{0};
    std::atomic<std::size_t> _task_id{0};
    std::atomic<std::size_t> _pending{0};

public:
    explicit sharded_task_queue(unsigned shard_count) : _shards(shard_count) {}

    auto enqueue(task_t&& f) -> enqueue_result_t {
        assert(!_shards.empty() && "Executor must have at least one shard.");

        const auto task_id = _task_id.fetch_add(1, std::memory_order_relaxed);
        const auto index = _index.fetch_add(1, std::memory_order_relaxed);
        for (unsigned n = 0; n != _shards.size(); ++n) {
            const auto shard = (index + n) % _shards.size();
            if (_shards[shard].try_push(task_id, std::move(f))) {
                _pending.fetch_add(1, std::memory_order_release);
                return {shard, task_id};
            }
        }

        const auto shard = index % _shards.size();
        _shards[shard].push(task_id, std::move(f));
        _pending.fetch_add(1, std::memory_order_release);
        return {shard, task_id};
    }

    auto try_pop(std::size_t hint) -> task_t {
        if (_shards.empty()) return nullptr;

        for (std::size_t n = 0; n != _shards.size(); ++n) {
            const auto shard = (hint + n) % _shards.size();
            if (auto task = _shards[shard].try_pop()) {
                _pending.fetch_sub(1, std::memory_order_acq_rel);
                return task;
            }
        }

        return nullptr;
    }

    auto reclaim(const enqueue_result_t& entry) -> task_t {
        auto result = _shards[entry._shard].reclaim(entry._id);
        if (result) _pending.fetch_sub(1, std::memory_order_acq_rel);
        return result;
    }

    auto pending() const -> std::size_t { return _pending.load(std::memory_order_acquire); }
};

auto portable_hardware_concurrency() -> unsigned {
#if STLAB_TASK_POOL_MAXIMUM() > 0
    return std::clamp(STLAB_TASK_POOL_MAXIMUM(), 1u, std::thread::hardware_concurrency());
#else
    return std::max(1u, std::thread::hardware_concurrency());
#endif
}

auto executor_shard_count() -> unsigned { return std::max(1u, portable_hardware_concurrency() - 1); }

class shared_executor_queues {
public:
    struct submission_t {
        executor_priority _priority;
        sharded_task_queue::enqueue_result_t _entry;
    };

private:
    std::array<sharded_task_queue, 3> _queues{
        sharded_task_queue{executor_shard_count()},
        sharded_task_queue{executor_shard_count()},
        sharded_task_queue{executor_shard_count()}};

public:
    auto submit(executor_priority priority, task<void() noexcept>&& task) -> submission_t {
        return {priority, _queues[executor_priority_index(priority)].enqueue(std::move(task))};
    }

    auto try_pop(executor_priority priority, std::size_t hint) -> task<void() noexcept> {
        return _queues[executor_priority_index(priority)].try_pop(hint);
    }

    auto reclaim(const submission_t& submission) -> task<void() noexcept> {
        return _queues[executor_priority_index(submission._priority)].reclaim(submission._entry);
    }

    auto pending(executor_priority priority) const -> std::size_t {
        return _queues[executor_priority_index(priority)].pending();
    }

    auto any_pending() const -> bool {
        return pending(executor_priority::high) != 0 || pending(executor_priority::medium) != 0 ||
               pending(executor_priority::low) != 0;
    }
};

auto executor_queues() -> shared_executor_queues& {
    static shared_executor_queues queues;
    return queues;
}

auto pack_hint(std::size_t hint) -> void* {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(hint));
}

auto unpack_hint(void* context) -> std::size_t {
    return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(context));
}

auto submission_hint(const shared_executor_queues::submission_t& submission) -> std::size_t {
    return submission._entry._shard;
}

template <executor_priority Priority, class Reschedule>
void run_wake(std::size_t hint, Reschedule&& reschedule) {
    if (auto task = executor_queues().try_pop(Priority, hint)) {
        task();
        return;
    }

    if (executor_queues().pending(Priority) != 0) std::forward<Reschedule>(reschedule)(hint);
}

template <class Schedule>
void submit_or_run_inline(executor_priority priority, task<void() noexcept>&& task, Schedule&& schedule) {
    auto submission = executor_queues().submit(priority, std::move(task));

    try {
        std::forward<Schedule>(schedule)(submission_hint(submission));
    } catch (...) {
        if (auto queued = executor_queues().reclaim(submission)) queued();
    }
}

} // namespace

#if STLAB_TASK_SYSTEM(LIBDISPATCH)

namespace {

template <executor_priority Priority>
void schedule_dispatch_wake(std::size_t hint);

template <executor_priority Priority>
void dispatch_wake(void* context) {
    run_wake<Priority>(unpack_hint(context), [](std::size_t next_hint) {
        schedule_dispatch_wake<Priority>(next_hint);
    });
}

template <executor_priority Priority>
void schedule_dispatch_wake(std::size_t hint) {
    dispatch_group_async_f(group()._group,
                           dispatch_get_global_queue(platform_priority(Priority), 0),
                           pack_hint(hint),
                           &dispatch_wake<Priority>);
}

} // namespace

auto group() -> const group_t& {
    static const group_t g = [] {
        group_t result;
        at_pre_exit([]() noexcept { dispatch_group_wait(group()._group, DISPATCH_TIME_FOREVER); });
        return result;
    }();

    return g;
}

group_t::~group_t() {
    if (_group) {
        dispatch_group_wait(_group, DISPATCH_TIME_FOREVER);
#if !STLAB_FEATURE(OBJC_ARC)
        dispatch_release(_group);
#endif
    }
}

void submit_executor_task(executor_priority priority, task<void() noexcept>&& f) {
    switch (priority) {
        case executor_priority::high:
            submit_or_run_inline(priority, std::move(f), [](std::size_t hint) {
                schedule_dispatch_wake<executor_priority::high>(hint);
            });
            break;
        case executor_priority::medium:
            submit_or_run_inline(priority, std::move(f), [](std::size_t hint) {
                schedule_dispatch_wake<executor_priority::medium>(hint);
            });
            break;
        case executor_priority::low:
            submit_or_run_inline(priority, std::move(f), [](std::size_t hint) {
                schedule_dispatch_wake<executor_priority::low>(hint);
            });
            break;
    }
}

#elif STLAB_TASK_SYSTEM(WINDOWS)

namespace {

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

template <executor_priority Priority>
class windows_wake_system;

template <executor_priority Priority>
auto wake_system() -> windows_wake_system<Priority>&;

template <executor_priority Priority>
class windows_wake_system {
    PTP_POOL _pool = nullptr;
    TP_CALLBACK_ENVIRON _callback_environment{};
    PTP_CLEANUP_GROUP _cleanup_group = nullptr;

public:
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

    windows_wake_system(const windows_wake_system&) = delete;
    auto operator=(const windows_wake_system&) -> windows_wake_system& = delete;

    ~windows_wake_system() {
        assert(_pool == nullptr && "stlab: Thread pool not joined prior to destruction.");
    }

    void schedule(std::size_t hint) {
        auto work = CreateThreadpoolWork(&callback, pack_hint(hint), &_callback_environment);
        if (work == nullptr) throw std::bad_alloc{};

        SubmitThreadpoolWork(work);
    }

    void join() {
        CloseThreadpoolCleanupGroupMembers(_cleanup_group, FALSE, nullptr);
        CloseThreadpoolCleanupGroup(_cleanup_group);
        CloseThreadpool(_pool);
        DestroyThreadpoolEnvironment(&_callback_environment);
        _cleanup_group = nullptr;
        _pool = nullptr;
    }

private:
    static void CALLBACK callback(PTP_CALLBACK_INSTANCE /*instance*/, PVOID parameter, PTP_WORK work) {
        run_wake<Priority>(unpack_hint(parameter), [](std::size_t next_hint) {
            wake_system<Priority>().schedule(next_hint);
        });
        CloseThreadpoolWork(work);
    }
};

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

void submit_executor_task(executor_priority priority, task<void() noexcept>&& f) {
    switch (priority) {
        case executor_priority::high:
            submit_or_run_inline(priority, std::move(f), [](std::size_t hint) {
                wake_system<executor_priority::high>().schedule(hint);
            });
            break;
        case executor_priority::medium:
            submit_or_run_inline(priority, std::move(f), [](std::size_t hint) {
                wake_system<executor_priority::medium>().schedule(hint);
            });
            break;
        case executor_priority::low:
            submit_or_run_inline(priority, std::move(f), [](std::size_t hint) {
                wake_system<executor_priority::low>().schedule(hint);
            });
            break;
    }
}

#elif STLAB_TASK_SYSTEM(PORTABLE)

struct priority_task_system::implementation {
    const unsigned _worker_count{executor_shard_count()};
    const unsigned _thread_limit{std::max(9U, portable_hardware_concurrency() * 4 + 1)};

    std::mutex _mutex;
    std::condition_variable _ready;
    std::vector<std::thread> _threads;
    std::size_t _waiting{0};
    std::size_t _wake_requests{0};
    bool _done{false};

    implementation() {
        _threads.reserve(_thread_limit);
        for (unsigned i = 0; i != _worker_count; ++i)
            add_thread_unlocked(i);
    }

    void submit(executor_priority priority, task<void() noexcept>&& f) {
        (void)executor_queues().submit(priority, std::move(f));
        std::unique_lock<std::mutex> lock{_mutex};
        _ready.notify_one();
    }

    auto wake() -> bool {
        std::unique_lock<std::mutex> lock{_mutex};
        if (_waiting == 0) return false;
        ++_wake_requests;
        _ready.notify_one();
        return true;
    }

    void add_thread() {
        std::unique_lock<std::mutex> lock{_mutex};
        if (_threads.size() == _thread_limit) return;
        add_thread_unlocked(_threads.size());
    }

    void join() {
        {
            std::unique_lock<std::mutex> lock{_mutex};
            _done = true;
        }
        _ready.notify_all();
        for (auto& thread : _threads)
            thread.join();
        _threads.clear();
    }

private:
    void add_thread_unlocked(std::size_t index) {
        _threads.emplace_back([this, index] {
            const auto name = index < _worker_count ? "cc.stlab.default_executor"
                                                    : "cc.stlab.default_executor.expansion";
            stlab::set_current_thread_name(name);

            while (true) {
                if (auto task = try_pop(index)) {
                    task();
                    continue;
                }

                if (wait(index)) return;
            }
        });
    }

    auto try_pop(std::size_t hint) -> task<void() noexcept> {
        if (auto task = executor_queues().try_pop(executor_priority::high, hint)) return task;
        if (auto task = executor_queues().try_pop(executor_priority::medium, hint)) return task;
        return executor_queues().try_pop(executor_priority::low, hint);
    }

    auto wait(std::size_t /*hint*/) -> bool {
        std::unique_lock<std::mutex> lock{_mutex};
        ++_waiting;
        _ready.wait(lock, [&] {
            return _done || _wake_requests != 0 || executor_queues().any_pending();
        });
        --_waiting;

        if (_done) return true;
        if (_wake_requests != 0) --_wake_requests;
        return false;
    }
};

priority_task_system::priority_task_system() : _impl(std::make_unique<implementation>()) {}

priority_task_system::~priority_task_system() = default;

void priority_task_system::submit(executor_priority priority, task<void() noexcept>&& f) {
    _impl->submit(priority, std::move(f));
}

auto priority_task_system::wake() -> bool { return _impl->wake(); }

void priority_task_system::add_thread() { _impl->add_thread(); }

void priority_task_system::join() { _impl->join(); }

auto pts() -> priority_task_system& {
    static priority_task_system only_task_system;
    static const auto registered = [] {
        at_pre_exit([]() noexcept { pts().join(); });
        return true;
    }();
    (void)registered;
    return only_task_system;
}

void submit_executor_task(executor_priority priority, task<void() noexcept>&& f) {
    pts().submit(priority, std::move(f));
}

#endif

} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {
extern "C" void stlab_v2_default_executor_submit(stlab_v2_task_proc proc, void* context) noexcept {
#if defined(_WIN32) && STLAB_TASK_SYSTEM(PORTABLE)
    if (proc == nullptr && context == reinterpret_cast<void*>(1)) {
        if (!STLAB_VERSION_NAMESPACE()::detail::pts().wake())
            STLAB_VERSION_NAMESPACE()::detail::pts().add_thread();
        return;
    }
#endif
    assert(proc != nullptr && "Task procedure must not be null.");
    try {
        STLAB_VERSION_NAMESPACE()::detail::submit_executor_task(
            STLAB_VERSION_NAMESPACE()::detail::executor_priority::medium,
            STLAB_VERSION_NAMESPACE()::task<void() noexcept>{[proc, context]() noexcept {
                proc(context);
            }});
    } catch (...) {
        proc(context);
    }
}

extern "C" void stlab_v2_high_executor_submit(stlab_v2_task_proc proc, void* context) noexcept {
    assert(proc != nullptr && "Task procedure must not be null.");
    try {
        STLAB_VERSION_NAMESPACE()::detail::submit_executor_task(
            STLAB_VERSION_NAMESPACE()::detail::executor_priority::high,
            STLAB_VERSION_NAMESPACE()::task<void() noexcept>{[proc, context]() noexcept {
                proc(context);
            }});
    } catch (...) {
        proc(context);
    }
}

extern "C" void stlab_v2_low_executor_submit(stlab_v2_task_proc proc, void* context) noexcept {
    assert(proc != nullptr && "Task procedure must not be null.");
    try {
        STLAB_VERSION_NAMESPACE()::detail::submit_executor_task(
            STLAB_VERSION_NAMESPACE()::detail::executor_priority::low,
            STLAB_VERSION_NAMESPACE()::task<void() noexcept>{[proc, context]() noexcept {
                proc(context);
            }});
    } catch (...) {
        proc(context);
    }
}

} // namespace v2
} // namespace stlab
