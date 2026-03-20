/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_SYSTEM_TIMER_HPP
#define STLAB_CONCURRENCY_SYSTEM_TIMER_HPP

/*! @file system_timer.hpp
 *  @brief System timer / delayed execution (platform run loop or portable thread).
 */

/**************************************************************************************************/

#include <cassert>
#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp>

#include <chrono>
#include <type_traits>

#if STLAB_TASK_SYSTEM(LIBDISPATCH)
#include <dispatch/dispatch.h>
#include <stlab/concurrency/default_executor.hpp>
#elif STLAB_TASK_SYSTEM(WINDOWS)
#include <Windows.h>
#include <memory>
#elif STLAB_TASK_SYSTEM(PORTABLE)
#include <algorithm>
#include <condition_variable>
#include <thread>
#include <vector>
#endif

#include <stlab/concurrency/task.hpp>

/**************************************************************************************************/

/** @defgroup stlab_concurrency_system_timer system_timer
 *  @ingroup stlab_concurrency
 *  @brief System timer / delayed execution (platform run loop or portable thread).
 *  @{
 */

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

#if STLAB_TASK_SYSTEM(LIBDISPATCH)

struct system_timer {
    inline static bool _closed{false};

    static inline auto mutex() -> std::mutex& {
        alignas(std::mutex) static std::array<unsigned char, sizeof(std::mutex)> _storage = {0};
        static std::mutex* _mutex = [&] { return new (&_storage[0]) std::mutex{}; }();
        return *_mutex;
    }

    static bool enter_group_if_open() {
        std::scoped_lock<std::mutex> lock(mutex());
        if (_closed) {
            return false;
        }
        dispatch_group_enter(detail::group()._group);
        return true;
    }

    static void set_closed() {
        std::scoped_lock<std::mutex> lock(mutex());
        _closed = true;
    }

    static bool is_closed() {
        std::scoped_lock<std::mutex> lock(mutex());
        return _closed;
    }

    system_timer() {
        // ensure the group is created and registered with pre_exit first
        (void)detail::group();
        at_pre_exit([]() noexcept { set_closed(); });
    }

    ~system_timer() {
        assert(is_closed() && "system_timer is not closed, pre_exit() was not called");
    }

    template <typename F, typename Rep, typename Per = std::ratio<1>>
    auto operator()(std::chrono::duration<Rep, Per> duration, F f) const
        -> std::enable_if_t<std::is_nothrow_invocable_v<F>> {
        assert(!is_closed() && "scheduling a task after pre_exit() was called");

        using namespace std::chrono;

        auto grouped = [f = std::move(f)]() mutable {
            // pre_exit tasks are executed in the reverse order of registration.
            // By entering the group before we check if the timer is closed we ensure that the
            // task is waited on if it starts.
            if (!enter_group_if_open()) {
                return;
            }
            std::move(f)();
            dispatch_group_leave(detail::group()._group);
        };

        using f_t = decltype(grouped);

        dispatch_after_f(dispatch_time(0, duration_cast<nanoseconds>(duration).count()),
                         dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                         new f_t(std::move(grouped)), [](void* f_) {
                             auto f = static_cast<f_t*>(f_);
                             (*f)();
                             delete f;
                         });
    }
};

/**************************************************************************************************/

#elif STLAB_TASK_SYSTEM(WINDOWS)

class system_timer {
    PTP_POOL _pool = nullptr;
    TP_CALLBACK_ENVIRON _callBackEnvironment;
    PTP_CLEANUP_GROUP _cleanupgroup = nullptr;

public:
    system_timer() {
        InitializeThreadpoolEnvironment(&_callBackEnvironment);
        _pool = CreateThreadpool(nullptr);
        if (_pool == nullptr) throw std::bad_alloc();

        _cleanupgroup = CreateThreadpoolCleanupGroup();
        if (_pool == nullptr) throw std::bad_alloc();

        SetThreadpoolCallbackPool(&_callBackEnvironment, _pool);
        SetThreadpoolCallbackCleanupGroup(&_callBackEnvironment, _cleanupgroup, nullptr);
    }

    ~system_timer() {
        CloseThreadpoolCleanupGroupMembers(_cleanupgroup, FALSE, nullptr);
        CloseThreadpoolCleanupGroup(_cleanupgroup);
        CloseThreadpool(_pool);
    }

    template <typename F>
    [[deprecated("Use chrono::duration as parameter instead")]] void operator()(
        std::chrono::steady_clock::time_point when, F&& f) {
        using namespace std::chrono;
        operator()(when - steady_clock::now(), std::forward<F>(f));
    }

    template <typename F, typename Rep, typename Per = std::ratio<1>>
    auto operator()(std::chrono::duration<Rep, Per> duration, F&& f)
        -> std::enable_if_t<std::is_nothrow_invocable_v<F>> {
        using namespace std::chrono;
        auto timer = CreateThreadpoolTimer(&timer_callback_impl<F>, new F(std::forward<F>(f)),
                                           &_callBackEnvironment);

        if (timer == nullptr) {
            throw std::bad_alloc();
        }

        auto file_time = duration_to_FILETIME(duration);

#pragma warning(push)
#pragma warning(disable : 6553) // bad annotation on SetThreadpoolTimer
        SetThreadpoolTimer(timer, &file_time, 0, 0);
#pragma warning(pop)
    }

private:
    template <typename F>
    static void CALLBACK timer_callback_impl(PTP_CALLBACK_INSTANCE /*Instance*/,
                                             PVOID parameter,
                                             PTP_TIMER /*timer*/) {
        std::unique_ptr<F> f(static_cast<F*>(parameter));
        (*f)();
    }

    template <typename Rep, typename Per = std::ratio<1>>
    FILETIME duration_to_FILETIME(std::chrono::duration<Rep, Per> duration) const {
        using namespace std::chrono;
        FILETIME ft = {0, 0};
        SYSTEMTIME st = {0};
        auto when = system_clock::now() + duration_cast<system_clock::duration>(duration);
        time_t t = system_clock::to_time_t(when);
        tm utc_tm;
        if (!gmtime_s(&utc_tm, &t)) {
            st.wSecond = static_cast<WORD>(utc_tm.tm_sec);
            st.wMinute = static_cast<WORD>(utc_tm.tm_min);
            st.wHour = static_cast<WORD>(utc_tm.tm_hour);
            st.wDay = static_cast<WORD>(utc_tm.tm_mday);
            st.wMonth = static_cast<WORD>(utc_tm.tm_mon + 1);
            st.wYear = static_cast<WORD>(utc_tm.tm_year + 1900);
            st.wMilliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(when.time_since_epoch())
                    .count() %
                1000;
            SystemTimeToFileTime(&st, &ft);
        }
        return ft;
    }
};

/**************************************************************************************************/

#elif STLAB_TASK_SYSTEM(PORTABLE)

class system_timer {
    using element_t = std::pair<std::chrono::steady_clock::time_point, task<void() noexcept>>;
    using queue_t = std::vector<element_t>;
    using lock_t = std::unique_lock<std::mutex>;

    queue_t _timed_queue;
    std::condition_variable _condition;
    bool _stop = false;
    std::mutex _timed_queue_mutex;
    std::thread _timed_queue_thread;

    struct greater_first {
        using result_type = bool;

        template <typename T>
        bool operator()(const T& x, const T& y) {
            return x.first > y.first;
        }
    };

    void timed_queue_run() {
        while (true) {
            task<void() noexcept> task;
            {
                lock_t lock(_timed_queue_mutex);

                while (_timed_queue.empty() && !_stop)
                    _condition.wait(lock);
                if (_stop) return;
                while (std::chrono::steady_clock::now() < _timed_queue.front().first) {
                    auto when = _timed_queue.front().first;
                    _condition.wait_until(lock, when);
                    if (_stop) return;
                }
                std::pop_heap(begin(_timed_queue), end(_timed_queue), greater_first());
                task = std::move(_timed_queue.back().second);
                _timed_queue.pop_back();
            }

            task();
        }
    }

public:
    system_timer() {
        _timed_queue_thread = std::thread([this] { this->timed_queue_run(); });
    }

    ~system_timer() {
        {
            lock_t lock(_timed_queue_mutex);
            _stop = true;
        }
        _condition.notify_one();
        _timed_queue_thread.join();
    }

    template <typename F>
    [[deprecated("Use chrono::duration as parameter instead")]] void operator()(
        std::chrono::steady_clock::time_point when, F&& f) {
        using namespace std::chrono;
        operator()(when - steady_clock::now(), std::forward<decltype(f)>(f));
    }

    template <typename F, typename Rep, typename Per = std::ratio<1>>
    auto operator()(std::chrono::duration<Rep, Per> duration, F&& f)
        -> std::enable_if_t<std::is_nothrow_invocable_v<F>> {
        lock_t lock(_timed_queue_mutex);
        _timed_queue.emplace_back(std::chrono::steady_clock::now() + duration, std::forward<F>(f));
        std::push_heap(std::begin(_timed_queue), std::end(_timed_queue), greater_first());
        _condition.notify_one();
    }
};

#endif

/**************************************************************************************************/

struct system_timer_type {
    using result_type = void;

    static auto get_system_timer() -> system_timer& {
        static system_timer only_system_timer;
        return only_system_timer;
    }

    [[deprecated("Use chrono::duration as parameter instead")]] void operator()(
        std::chrono::steady_clock::time_point when, task<void() noexcept>&& f) const {
        operator()(when - std::chrono::steady_clock().now(), std::move(f));
    }

    template <typename Rep, typename Per = std::ratio<1>>
    void operator()(std::chrono::duration<Rep, Per> duration, task<void() noexcept>&& f) const {
        get_system_timer()(duration, std::move(f));
    }
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

/// Schedules `void() noexcept` tasks on a system timer / run loop (platform-dependent).
inline constexpr auto system_timer = detail::system_timer_type{};

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

/** @} */

#endif

/**************************************************************************************************/
