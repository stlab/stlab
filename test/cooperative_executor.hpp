/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_TEST_COOPERATIVE_EXECUTOR_HPP
#define STLAB_TEST_COOPERATIVE_EXECUTOR_HPP

#include <stlab/concurrency/task.hpp>

#include <deque>

/**************************************************************************************************/

namespace stlab {
namespace test {

/**************************************************************************************************/

/// Executor that enqueues tasks in a deque for cooperative execution by tests.
/// Use execute_one() to run a single task, or execute_all() to drain the queue.
class cooperative_executor {
    std::deque<task<void() noexcept>> _tasks;

public:
    /// Returns an executor that enqueues tasks in the queue. Cannot outlive
    /// the cooperative_executor.
    auto executor() {
        return [this](auto&& f) { _tasks.push_back(std::forward<decltype(f)>(f)); };
    }

    /// Execute one task from the front of the queue. No-op if empty.
    void execute_one() {
        if (_tasks.empty()) return;
        auto t = std::move(_tasks.front());
        _tasks.pop_front();
        t();
    }

    /// Execute all tasks currently in the queue. Tasks added by executed tasks
    /// are also run until the queue is empty.
    void execute_all() {
        while (!_tasks.empty())
            execute_one();
    }

    bool empty() const noexcept { return _tasks.empty(); }
    auto size() const noexcept -> std::size_t { return _tasks.size(); }
};

/**************************************************************************************************/

} // namespace test
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
