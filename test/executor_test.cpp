/*
    Copyright 2013 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/serial_queue.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

#include <doctest/doctest.h>

using namespace stlab;
using namespace std;

namespace {
void rest() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

struct counted_task_context {
    std::atomic<int>* _count{nullptr};
    std::atomic<int>* _remaining{nullptr};
    std::condition_variable* _ready{nullptr};
    std::mutex* _mutex{nullptr};

    static void run(void* context) noexcept {
        auto& self = *static_cast<counted_task_context*>(context);
        self._count->fetch_add(1, std::memory_order_relaxed);

        if (self._remaining->fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::scoped_lock lock{*self._mutex};
            self._ready->notify_one();
        }
    }
};

template <typename Submit>
void wait_for_all_submissions(Submit&& submit, std::size_t count) {
    std::vector<std::atomic<int>> executions(count);
    for (auto& execution : executions)
        execution.store(0, std::memory_order_relaxed);

    std::vector<counted_task_context> contexts(count);
    std::atomic<int> remaining{static_cast<int>(count)};
    std::condition_variable ready;
    std::mutex mutex;

    for (std::size_t i = 0; i < count; ++i) {
        contexts[i] = counted_task_context{&executions[i], &remaining, &ready, &mutex};
        task<void() noexcept> t{
            [context = &contexts[i]]() noexcept { counted_task_context::run(context); }};
        submit(t.relocation_concept(), t.relocation_invoke(), t.relocation_source(), i);
    }

    {
        std::unique_lock<std::mutex> lock{mutex};
        ready.wait(lock, [&] { return remaining.load(std::memory_order_acquire) == 0; });
    }

    for (const auto& execution : executions) {
        REQUIRE(execution.load(std::memory_order_relaxed) == 1);
    }
}
} // namespace

TEST_CASE("all_low_prio_tasks_are_executed") {
    serial_queue_t queue(low_executor);
    mutex m;
    vector<int> results;
    atomic_bool done{false};

    for (auto i = 0; i < 10; ++i) {
        queue.executor()([_i = i, &m, &results]() noexcept {
            unique_lock<mutex> block{m};
            results.push_back(_i);
        });
    }
    queue.executor()([&done]() noexcept { done = true; });

    while (!done) {
        rest();
    }

    for (auto i = 0; i < 10; ++i) {
        REQUIRE(results[i] == i);
    }
}

TEST_CASE("all_default_prio_tasks_get_executed") {
    serial_queue_t queue(default_executor);
    mutex m;
    vector<int> results;
    atomic_bool done{false};

    for (auto i = 0; i < 10; ++i) {
        queue.executor()([_i = i, &m, &results]() noexcept {
            unique_lock<mutex> block{m};
            results.push_back(_i);
        });
    }
    queue.executor()([&done]() noexcept { done = true; });

    while (!done) {
        rest();
    }

    for (auto i = 0; i < 10; ++i) {
        REQUIRE(results[i] == i);
    }
}

TEST_CASE("all_high_prio_tasks_get_executed") {
    serial_queue_t queue(high_executor);
    mutex m;
    vector<int> results;
    atomic_bool done{false};

    for (auto i = 0; i < 10; ++i) {
        queue.executor()([_i = i, &m, &results]() noexcept {
            unique_lock<mutex> block{m};
            results.push_back(_i);
        });
    }
    queue.executor()([&done]() noexcept { done = true; });

    while (!done) {
        rest();
    }

    for (auto i = 0; i < 10; ++i) {
        REQUIRE(results[i] == i);
    }
}

TEST_CASE("task_system_restarts_after_it_went_pending") {
    bool done{false};
    mutex m;
    condition_variable cv;

    default_executor([&]() noexcept {
        rest();
        {
            unique_lock<mutex> block{m};
            done = true;
        }
        cv.notify_one();
    });

    {
        unique_lock<mutex> block{m};
        invoke_waiting([&] { cv.wait(block, [&] { return done; }); });
    }

    default_executor([&]() noexcept {
        rest();
        {
            unique_lock<mutex> block{m};
            done = false;
            cv.notify_one();
        }
    });

    {
        unique_lock<mutex> block{m};
        invoke_waiting([&] { cv.wait(block, [&] { return !done; }); });
    }

    REQUIRE(!done);
}

TEST_CASE("abi_executor_submit_executes_each_task_exactly_once_across_priorities") {
    wait_for_all_submissions(
        [](const task<void() noexcept>::concept_t* vtable, task<void() noexcept>::invoke_t invoke,
           void* source, std::size_t index) {
            switch (index % 3) {
                case 0:
                    stlab_v2_high_executor_submit(vtable, invoke, source);
                    break;
                case 1:
                    stlab_v2_default_executor_submit(vtable, invoke, source);
                    break;
                case 2:
                    stlab_v2_low_executor_submit(vtable, invoke, source);
                    break;
            }
        },
        96);
}

TEST_CASE("abi_executor_submit_drains_concurrent_contention_without_dropping_tasks") {
    constexpr std::size_t submitter_count = 8;
    constexpr std::size_t tasks_per_submitter = 128;

    std::vector<std::atomic<int>> executions(submitter_count * tasks_per_submitter);
    for (auto& execution : executions)
        execution.store(0, std::memory_order_relaxed);

    std::vector<counted_task_context> contexts(executions.size());
    std::atomic<int> remaining{static_cast<int>(contexts.size())};
    std::condition_variable ready;
    std::mutex mutex;

    for (std::size_t i = 0; i < contexts.size(); ++i) {
        contexts[i] = counted_task_context{&executions[i], &remaining, &ready, &mutex};
    }

    std::vector<std::thread> submitters;
    submitters.reserve(submitter_count);

    for (std::size_t submitter = 0; submitter < submitter_count; ++submitter) {
        submitters.emplace_back([&, submitter] {
            const auto base = submitter * tasks_per_submitter;

            for (std::size_t offset = 0; offset < tasks_per_submitter; ++offset) {
                auto* context = &contexts[base + offset];
                task<void() noexcept> t{
                    [context]() noexcept { counted_task_context::run(context); }};
                switch ((submitter + offset) % 3) {
                    case 0:
                        stlab_v2_high_executor_submit(t.relocation_concept(), t.relocation_invoke(),
                                                      t.relocation_source());
                        break;
                    case 1:
                        stlab_v2_default_executor_submit(
                            t.relocation_concept(), t.relocation_invoke(), t.relocation_source());
                        break;
                    case 2:
                        stlab_v2_low_executor_submit(t.relocation_concept(), t.relocation_invoke(),
                                                     t.relocation_source());
                        break;
                }
            }
        });
    }

    for (auto& submitter : submitters)
        submitter.join();

    {
        std::unique_lock<std::mutex> lock{mutex};
        ready.wait(lock, [&] { return remaining.load(std::memory_order_acquire) == 0; });
    }

    for (const auto& execution : executions) {
        REQUIRE(execution.load(std::memory_order_relaxed) == 1);
    }
}

#if STLAB_TASK_SYSTEM(PORTABLE)
TEST_CASE("invoke_waiting_on_portable_pool_completes_nested_submissions") {
    constexpr std::size_t outer_task_count = 8;
    constexpr std::size_t iterations_per_task = 64;

    std::condition_variable ready;
    std::mutex mutex;
    std::atomic<std::size_t> completed{0};
    std::atomic_bool timed_out{false};

    for (std::size_t outer = 0; outer < outer_task_count; ++outer) {
        auto completion =
            std::shared_ptr<void>{nullptr, [&](void*) noexcept {
                                      std::scoped_lock lock{mutex};
                                      completed.fetch_add(1, std::memory_order_acq_rel);
                                      ready.notify_all();
                                  }};

        default_executor([&, completion = std::move(completion)]() noexcept {
            (void)completion;
            for (std::size_t iteration = 0; iteration < iterations_per_task; ++iteration) {
                auto inner_done = std::make_shared<std::atomic_bool>(false);
                default_executor([&, inner_done]() noexcept {
                    std::scoped_lock lock{mutex};
                    inner_done->store(true, std::memory_order_release);
                    ready.notify_all();
                });

                std::unique_lock<std::mutex> lock{mutex};
                if (!invoke_waiting([&] {
                        return ready.wait_for(lock, std::chrono::seconds(5), [&] {
                            return inner_done->load(std::memory_order_acquire);
                        });
                    })) {
                    timed_out.store(true, std::memory_order_release);
                    break;
                }
            }
        });
    }

    std::unique_lock<std::mutex> lock{mutex};
    const auto finished = ready.wait_for(lock, std::chrono::seconds(30), [&] {
        return completed.load(std::memory_order_acquire) == outer_task_count;
    });

    REQUIRE(finished);
    REQUIRE(!timed_out.load(std::memory_order_acquire));
}
#endif

// REVISIT (sean-parent) - These tests is disabled because boost multi-precision is generated
// deprecated warnings.
#if 0

namespace {
auto fiboN{1000};
const auto iterations = 100'000;
const auto startCount = 100;
atomic_int workToDo{(iterations - startCount) * 3};
const auto expectedWork = startCount * 3 + workToDo;
atomic_int highCount{0};
atomic_int defaultCount{0};
atomic_int lowCount{0};

atomic_int taskRunning{0};


enum class executor_priority : std::uint8_t { high, medium, low };

template <executor_priority P>
struct check_task {
    atomic_int& _correctScheduleCount;
    atomic_int& _currentPrioCount;

    check_task(atomic_int& correctCount, atomic_int& prioCount) :
        _correctScheduleCount(correctCount), _currentPrioCount(prioCount) {
        ++_currentPrioCount;
    }

    template <executor_priority Q>
    auto schedule_increment() -> std::enable_if_t<Q == executor_priority::low, int> {
        return static_cast<int>(highCount <= taskRunning && defaultCount <= taskRunning);
    }

    template <executor_priority Q>
    auto schedule_increment() -> std::enable_if_t<Q == executor_priority::medium, int> {
        return static_cast<int>(highCount <= taskRunning);
    }

    template <executor_priority Q>
    auto schedule_increment() -> std::enable_if_t<Q == executor_priority::high, int> {
        return 0;
    }

    void operator()() noexcept {
        --_currentPrioCount;

        ++taskRunning;

        _correctScheduleCount += schedule_increment<P>();

        fibonacci<mpre::cpp_int>(fiboN);

        switch (workToDo % 3) {
            case 0:
                default_executor(
                    check_task<executor_priority::medium>{correctDefault, defaultCount});
                break;

            case 1:
                high_executor(check_task<executor_priority::high>{correctHigh, highCount});
                break;

            case 2:
                low_executor(check_task<executor_priority::low>{correctLow, lowCount});
                break;
        }
        --workToDo;

        ++done;
        --taskRunning;
    }
};
} // namespace

TEST_CASE("all_tasks_will_be_executed_according_to_their_prio") {
    auto start = chrono::high_resolution_clock::now();

    for (auto i = 0; i < startCount; ++i) {
        low_executor(check_task<executor_priority::low>{correctLow, lowCount});
        high_executor(check_task<executor_priority::high>{correctHigh, highCount});
        default_executor(check_task<executor_priority::medium>{correctDefault, defaultCount});
    }
    while (done < expectedWork) {
        rest();
    }

    auto stop = std::chrono::high_resolution_clock::now();
    std::cout << "\nPerformance measuring: " << std::chrono::duration<double>(stop - start).count()
              << "s\n";

    // REVISIT (sean-parent) - I don't believe that this is measuring the probability of a task
    // executing in order.stl
    cout << "Correct low ordering:     "
         << static_cast<double>(correctLow.load()) / iterations * 100.0 << "%\n";
    cout << "Correct default ordering: "
         << static_cast<double>(correctDefault.load()) / iterations * 100.0 << "%\n";
    cout << "Correct high ordering: "
         << static_cast<double>(correctHigh.load()) / iterations * 100.0 << "%\n";
}

TEST_CASE("MeasureTiming") {
    std::vector<int> results;
    const auto iterations = 10'000;
    results.resize(iterations * 3);
    atomic_bool done{false};
    condition_variable ready;
    atomic_int counter{0};

    auto start = chrono::high_resolution_clock::now();

    for (auto i = 0; i < iterations; ++i) {
        low_executor([_i = i, &results, &counter]() noexcept {
            results[_i] = 1;
            fibonacci<mpre::cpp_int>(fiboN);
            ++counter;
        });
        default_executor([_i = i + iterations, &results, &counter]() noexcept {
            results[_i] = 2;
            fibonacci<mpre::cpp_int>(fiboN);
            ++counter;
        });
        high_executor([_i = i + iterations * 2, &results, &counter]() noexcept {
            results[_i] = 3;
            fibonacci<mpre::cpp_int>(fiboN);
            ++counter;
        });
    }

    mutex block;
    low_executor([&]() noexcept {
        {
            unique_lock<mutex> lock{block};
            done = true;

            ready.notify_one();
        }
    });

    invoke_waiting([&] {
        unique_lock<mutex> lock{block};
        ready.wait(lock, [&]{ ready.wait(lock, [&]{ return done; }); });
    });

    while (counter < 3 * iterations) {
        rest();
    }

    auto stop = std::chrono::high_resolution_clock::now();
    std::cout << "\nPerformance measuring: " << std::chrono::duration<double>(stop - start).count()
              << "s\n";
}
#endif
