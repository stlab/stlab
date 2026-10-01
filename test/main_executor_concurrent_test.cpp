/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract: every task submitted concurrently from many threads runs exactly once on the main
// queue, and tasks from one submitting thread run in that thread's submission order.

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>

#include <condition_variable>
#include <cstddef>
#include <mutex>
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
bool submitters_ready = false;

// Published by the startup task after every submitter has been constructed.
std::vector<std::thread> submitters;

#if defined(__EMSCRIPTEN_PTHREADS__)
std::mutex startup_mutex;
std::condition_variable startup_condition;
bool startup_task_ran = false;
#endif

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
#if defined(__EMSCRIPTEN_PTHREADS__)
        // PROXY_TO_PTHREAD already services the main runtime during host startup.
        stlab::main_executor([]() noexcept {
            main_executor_test::require_run_started();
            std::scoped_lock lock{startup_mutex};
            startup_task_ran = true;
            startup_condition.notify_one();
        });
        {
            std::unique_lock<std::mutex> lock{startup_mutex};
            startup_condition.wait(lock, [] { return startup_task_ran; });
        }
#endif
        submitters.reserve(thread_count);
        for (int t = 0; t != thread_count; ++t) {
            submitters.emplace_back([t] {
                for (int i = 0; i != tasks_per_thread; ++i) {
                    stlab::main_executor([t, i]() noexcept {
                        main_executor_test::require_run_started();
                        ++runs[static_cast<std::size_t>(t * tasks_per_thread + i)];
                        per_thread_fifo = per_thread_fifo && last_seen[t] == i - 1;
                        last_seen[t] = i;
                        if (++completed == total && submitters_ready) check_and_finish();
                    });
                }
            });
        }
        stlab::main_executor([]() noexcept {
            submitters_ready = true;
            if (completed == total) check_and_finish();
        });
    });
}
