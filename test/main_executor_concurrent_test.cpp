/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract: every task submitted concurrently from many threads runs exactly once on the main
// queue, and tasks from one submitting thread run in that thread's submission order.

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>

#include <cstddef>
#include <thread>
#include <vector>

namespace {

constexpr int thread_count = 8;
constexpr int tasks_per_thread = 1000;
constexpr int total = thread_count * tasks_per_thread;

// Accessed only from the main queue.
std::vector<int> runs(total, 0);
std::vector<int> last_seen(thread_count, -1);
bool per_thread_fifo = true;
int completed = 0;

// Written by `main()` before submitters start; joined from the final task.
std::vector<std::thread> submitters;

void check_and_finish() noexcept {
    for (auto& t : submitters)
        t.join();
    for (int n : runs) {
        if (n != 1) main_executor_test::finish(false, "a task did not run exactly once");
    }
    if (!per_thread_fifo) main_executor_test::finish(false, "per-thread order was not preserved");
    main_executor_test::finish(true, "");
}

} // namespace

int main(int argc, char** argv) {
    main_executor_test::run(argc, argv, [] {
        submitters.reserve(thread_count);
        for (int t = 0; t != thread_count; ++t) {
            submitters.emplace_back([t] {
                for (int i = 0; i != tasks_per_thread; ++i) {
                    stlab::main_executor([t, i]() noexcept {
                        main_executor_test::require_run_started();
                        ++runs[static_cast<std::size_t>(t * tasks_per_thread + i)];
                        per_thread_fifo = per_thread_fifo && last_seen[t] == i - 1;
                        last_seen[t] = i;
                        if (++completed == total) check_and_finish();
                    });
                }
            });
        }
    });
}
