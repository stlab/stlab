/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <stlab/concurrency/default_executor.hpp>
#include <stlab/pre_exit.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <doctest/doctest.h>

using namespace stlab;

namespace {

struct smoke_context {
    std::atomic<bool>* _done{nullptr};
    std::condition_variable* _ready{nullptr};
    std::mutex* _mutex{nullptr};

    static void run(void* context) noexcept {
        auto& self = *static_cast<smoke_context*>(context);
        self._done->store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock{*self._mutex};
        self._ready->notify_one();
    }
};

std::atomic<int> pre_exit_calls{0};

void record_pre_exit_call() noexcept { pre_exit_calls.fetch_add(1, std::memory_order_relaxed); }

} // namespace

TEST_CASE("shared core exports blocking notification") {
    stlab_v2_notify_default_executor_before_waiting();
}

TEST_CASE("shared core exports execute work and preserve pre_exit") {
    std::atomic<bool> done{false};
    std::condition_variable ready;
    std::mutex mutex;
    smoke_context context{&done, &ready, &mutex};

    task<void() noexcept> t{[&context]() noexcept { smoke_context::run(&context); }};
    stlab_v2_default_executor_submit(t.relocation_concept(), t.relocation_invoke(),
                                     t.relocation_source());

    {
        std::unique_lock<std::mutex> lock{mutex};
        ready.wait(lock, [&] { return done.load(std::memory_order_acquire); });
    }

    stlab_at_pre_exit(&record_pre_exit_call);
    stlab_pre_exit();

    CHECK(pre_exit_calls.load(std::memory_order_relaxed) == 1);
}
