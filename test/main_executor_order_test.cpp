/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

// Contract: tasks run in submission order on the main queue; tasks submitted before
// `main_executor_run()` run after it starts; the main queue is serviced on the thread that calls
// `main_executor_run()` (Emscripten: the main runtime thread).

#include "main_executor_test_host.hpp"

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/config.hpp>

#include <cstddef>
#include <thread>
#include <vector>

namespace {

constexpr int task_count = 1000;

std::vector<int> order;
std::thread::id main_queue_thread;
bool single_thread = true;

} // namespace

int main(int argc, char** argv) {
    const auto run_thread = std::this_thread::get_id();

    main_executor_test::run(argc, argv, [run_thread] {
        order.reserve(task_count);
        for (int i = 0; i != task_count; ++i) {
            stlab::main_executor([i]() noexcept {
                main_executor_test::require_run_started();
                if (order.empty()) main_queue_thread = std::this_thread::get_id();
                single_thread = single_thread && main_queue_thread == std::this_thread::get_id();
                order.push_back(i);
            });
        }

        stlab::main_executor([run_thread]() noexcept {
            main_executor_test::require_run_started();
            bool in_order = order.size() == static_cast<std::size_t>(task_count);
            for (int i = 0; in_order && i != task_count; ++i)
                in_order = order[i] == i;

            bool on_run_thread = main_queue_thread == run_thread;
#if STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
            on_run_thread = true; // The Emscripten main queue is the main runtime thread.
#endif
            if (!in_order) main_executor_test::finish(false, "tasks did not run in FIFO order");
            if (!single_thread) main_executor_test::finish(false, "tasks ran on multiple threads");
            if (!on_run_thread) main_executor_test::finish(false, "tasks ran off the run() thread");
            main_executor_test::finish(true, "");
        });
    });
}
