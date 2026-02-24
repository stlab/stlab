/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <thread>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/channel.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>

#include "channel_test_helper.hpp"

using namespace stlab;

using channel_test_fixture_int_1 = channel_test_helper::channel_test_fixture<int, 1>;

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_process_with_one_step") {
    std::atomic_int index{0};
    std::vector<int> results(10, 0);

    auto check = _receive[0] | channel_test_helper::sum<1>() | [&](int x) {
        results[index] = x;
        ++index;
    };

    _receive[0].set_ready();
    for (auto i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&] { return index == 10; });

    for (auto i = 0; i < 10; ++i) {
        REQUIRE(results[i] == i);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_process_with_one_step_async") {
    std::atomic_int index{0};
    std::vector<int> results(10, 0);

    auto check = _receive[0] | channel_test_helper::sum<1>() | [&](int x) {
        results[x] = x;
        ++index;
    };

    _receive[0].set_ready();
    std::vector<stlab::future<void>> f(10);
    for (auto i = 0; i < 10; ++i) {
        f.push_back(stlab::async(stlab::default_executor, [_send = _send[0], i] { _send(i); }));
    }

    wait_until_done([&] { return index == 10; });

    for (auto i = 0; i < 10; ++i) {
        REQUIRE(std::find(results.begin(), results.end(), i) != results.end());
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_process_with_two_steps") {
    std::atomic_int index{0};
    std::vector<int> results(5, 0);

    auto check = _receive[0] | channel_test_helper::sum<2>() | [&](int x) {
        results[index] = x;
        ++index;
    };

    _receive[0].set_ready();
    for (auto i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&] { return index == 5; });

    std::array expectation{1, 5, 9, 13, 17};
    for (auto i = 0; i < 5; ++i) {
        REQUIRE(results[i] == expectation[i]);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_process_with_two_steps_async") {
    std::atomic_int index{0};
    std::vector<std::vector<int>> results;

    auto check =
        _receive[0] | channel_test_helper::collector<2>() | [&](const std::vector<int>& x) {
            results.push_back(x);
            ++index;
        };

    _receive[0].set_ready();
    std::vector<stlab::future<void>> f(10);
    for (auto i = 0; i < 10; ++i) {
        f.push_back(stlab::async(stlab::default_executor, [_send = _send[0], i] { _send(i); }));
    }

    wait_until_done([&] { return index == 5; });

    std::vector<int> expectations = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    REQUIRE(results.size() == std::size_t(5));

    for (const auto& c : results) {
        REQUIRE(c.size() == std::size_t(2));
        for (auto i : c) {
            auto it = std::find(expectations.begin(), expectations.end(), i);
            REQUIRE(it != expectations.end());
            expectations.erase(it);
        }
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_process_with_many_steps") {
    std::atomic_int result{0};

    auto check = _receive[0] | channel_test_helper::sum<10>() | [&](int x) { result = x; };

    _receive[0].set_ready();
    for (auto i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&] { return result != 0; });

    REQUIRE(result == 45);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_process_with_many_steps_async") {
    std::atomic_int result{0};

    auto check = _receive[0] | channel_test_helper::sum<10>() | [&](int x) { result = x; };

    _receive[0].set_ready();
    std::vector<stlab::future<void>> f(10);
    for (auto i = 0; i < 10; ++i) {
        f.push_back(stlab::async(stlab::default_executor, [_send = _send[0], i] { _send(i); }));
    }

    wait_until_done([&] { return result != 0; });

    REQUIRE(result == 45);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_split_process_one_step") {
    std::atomic_int index1{0};
    std::vector<int> results1(10, 0);
    std::atomic_int index2{0};
    std::vector<int> results2(10, 0);

    auto check1 = _receive[0] | channel_test_helper::sum<1>() |
                  [&_index = index1, &_results = results1](int x) {
                      _results[x] = x;
                      ++_index;
                  };
    auto check2 = _receive[0] | channel_test_helper::sum<1>() |
                  [&_index = index2, &_results = results2](int x) {
                      _results[x] = x;
                      ++_index;
                  };

    _receive[0].set_ready();
    for (auto i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&] { return index1 == 10 && index2 == 10; });

    for (auto i = 0; i < 10; ++i) {
        REQUIRE(results1[i] == i);
        REQUIRE(results2[i] == i);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_split_process_two_steps") {
    std::atomic_int index1{0};
    std::vector<int> results1(5);
    std::atomic_int index2{0};
    std::vector<int> results2(5);

    auto check1 = _receive[0] | channel_test_helper::sum<2>() |
                  [&_index = index1, &_results = results1](int x) {
                      _results[_index] = x;
                      ++_index;
                  };
    auto check2 = _receive[0] | channel_test_helper::sum<2>() |
                  [&_index = index2, &_results = results2](int x) {
                      _results[_index] = x;
                      ++_index;
                  };

    _receive[0].set_ready();
    for (auto i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&] { return index1 == 5 && index2 == 5; });

    const std::array expectation{1, 5, 9, 13, 17};
    for (auto i = 0; i < 5; ++i) {
        REQUIRE(results1[i] == expectation[i]);
        REQUIRE(results2[i] == expectation[i]);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_split_process_many_steps") {
    std::atomic_int result1{0};
    std::atomic_int result2{0};

    auto check1 =
        _receive[0] | channel_test_helper::sum<10>() | [&_result = result1](int x) { _result = x; };
    auto check2 =
        _receive[0] | channel_test_helper::sum<10>() | [&_result = result2](int x) { _result = x; };

    _receive[0].set_ready();
    for (auto i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&] { return result1 != 0 && result2 != 0; });

    REQUIRE(result1 == 45);
    REQUIRE(result2 == 45);
}

TEST_CASE("int_channel_process_with_two_steps_timed") {
    std::atomic_int result{0};
    stlab::sender<int> send;
    stlab::receiver<int> receive;

    std::tie(send, receive) = stlab::channel<int>(channel_test_helper::manual_scheduler());

    auto check = receive | channel_test_helper::timed_sum() | [&](int x) { result = x; };

    receive.set_ready();
    send(42);

    channel_test_helper::manual_scheduler::run_next_task();

    channel_test_helper::manual_scheduler::wait_until_queue_size_of(1);
    channel_test_helper::manual_scheduler::run_next_task();

    channel_test_helper::manual_scheduler::wait_until_queue_size_of(1);
    channel_test_helper::manual_scheduler::run_next_task();

    channel_test_helper::manual_scheduler::wait_until_queue_size_of(1);
    channel_test_helper::manual_scheduler::run_next_task();

    while (result == 0) {
        invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1,
                  "int_channel_process_with_two_steps_timed_wo_timeout") {
    std::atomic_int result{0};
    stlab::sender<int> send;
    stlab::receiver<int> receive;

    std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);

    auto check = receive | channel_test_helper::timed_sum(2) | [&](int x) { result = x; };

    receive.set_ready();
    send(42);

    while (channel_test_helper::timed_sum::current_sum() != 42) {
        invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    send(43);

    while (result == 0) {
        invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    REQUIRE(result == 85);
}

namespace {
struct process_with_set_error {
    explicit process_with_set_error(std::atomic_bool& check) : _check(check) {}

    std::atomic_bool& _check;

    static void await(int /*unused*/) { throw std::runtime_error{""}; }

    void set_error(const std::exception_ptr /*unused*/&) { _check = true; }

    static auto yield() -> int { return 42; }

    [[nodiscard]] static auto state() { return stlab::await_forever; }
};

bool always_true{true}; // used to avoid unused variable warning

} // namespace

TEST_CASE("int_channel_process_set_error_is_called_on_upstream_error") {
    std::atomic_bool check{false};
    stlab::sender<int> send;
    stlab::receiver<int> receive;

    std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);

    auto result = receive |
                  [](auto v) {
                      if (always_true) throw std::runtime_error{""};
                      return v;
                  } |
                  process_with_set_error{check} | [](int) {};

    receive.set_ready();
    send(42);

    while (!check) {
        invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    REQUIRE(check.load());
}

namespace {
struct process_with_close {
    explicit process_with_close(std::atomic_bool& check) : _check(check) {}

    std::atomic_bool& _check;

    static void await(int /*unused*/) { throw std::runtime_error{""}; }

    void close() { _check = true; }

    static auto yield() -> int { return 42; }

    [[nodiscard]] static auto state() { return stlab::await_forever; }
};
} // namespace

TEST_CASE("int_channel_process_close_is_called_on_upstream_error") {
    std::atomic_bool check{false};
    stlab::sender<int> send;
    stlab::receiver<int> receive;

    std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);

    auto result = receive |
                  [](auto v) {
                      if (always_true) throw std::runtime_error{""};
                      return v;
                  } |
                  process_with_close{check} | [](int) {};

    receive.set_ready();
    send(42);

    while (!check) {
        invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    REQUIRE(check.load());
}
