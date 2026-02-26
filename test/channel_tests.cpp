/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/executor_base.hpp>
#include <stlab/concurrency/task.hpp>

#ifndef STLAB_DISABLE_FUTURE_COROUTINES
#define STLAB_DISABLE_FUTURE_COROUTINES
#endif

#include <queue>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/channel.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/test/model.hpp>

#include "channel_test_helper.hpp"

using namespace stlab;

TEST_CASE("int_sender") {
    stlab::sender<int> const send;

    REQUIRE_NOTHROW(send(42));
}

TEST_CASE("int_receiver") {
    stlab::receiver<int> receive;

    REQUIRE(receive.ready() == false);

    receive.set_ready();
    REQUIRE(receive.ready() == true);
}

using channel_test_fixture_int_1 = channel_test_helper::channel_test_fixture<int, 1>;

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel") {
    REQUIRE(_receive[0].ready() == false);

    _receive[0].set_ready();
    REQUIRE(_receive[0].ready() == true);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_move_ctor_sender") {
    // "int channel move ctor sender");

    std::atomic_int result{0};

    auto check = _receive[0] | [&](int x) { result = x; };
    auto sut = std::move(_send[0]);

    _receive[0].set_ready();
    sut(42);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_move_assignment_sender") {
    // "int channel move assignment sender");

    stlab::sender<int> sut;
    std::atomic_int result{0};

    auto check = _receive[0] | [&](int x) { result = x; };
    sut = std::move(_send[0]);

    _receive[0].set_ready();
    sut(42);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_copy_ctor_sender") {
    // "int channel copy ctor sender");

    std::atomic_int result{0};

    auto check = _receive[0] | [&](int x) { result = x; };
    auto sut(_send[0]);

    _receive[0].set_ready();
    sut(42);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);

    result = 0;

    _send[0](4711);

    wait_until_done([&] { return result == 4711; });

    REQUIRE(result == 4711);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_copy_assignment_sender") {
    // "int channel copy assignment sender");

    stlab::sender<int> sut;
    std::atomic_int result{0};

    auto check = _receive[0] | [&](int x) { result = x; };
    sut = _send[0];

    _receive[0].set_ready();
    sut(42);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);

    result = 0;

    _send[0](4711);

    wait_until_done([&] { return result == 4711; });

    REQUIRE(result == 4711);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_move_ctor_receiver") {
    // "int channel move ctor receiver");

    std::atomic_int result{0};

    auto sut = std::move(_receive[0]);

    auto check = sut | [&](int x) { result = x; };
    sut.set_ready();
    _send[0](42);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_move_assignment_receiver") {
    // "int channel move assignment receiver");

    stlab::receiver<int> sut;
    std::atomic_int result{0};

    sut = std::move(_receive[0]);
    auto check = sut | [&](int x) { result = x; };

    sut.set_ready();
    _send[0](42);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_copy_ctor_receiver") {
    // "int channel copy ctor receiver");

    std::atomic_int result{0};

    auto sut(_receive[0]);

    auto check = sut | [&](int x) { result = x; };

    _receive[0].set_ready();
    sut.set_ready();
    _send[0](42);

    wait_until_done([&]() { return result == 42; });

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_copy_assignment_receiver") {
    // "int channel copy assignment receiver");

    stlab::receiver<int> sut;
    std::atomic_int result{0};

    sut = _receive[0];

    auto check = sut | [&](int x) { result = x; };

    _receive[0].set_ready();
    sut.set_ready();

    _send[0](42);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_concatenate_two_channels") {
    std::atomic_int result{0};
    stlab::sender<int> A;
    stlab::sender<int> X;
    stlab::receiver<int> B;
    stlab::receiver<int> Y;
    std::tie(A, B) = stlab::channel<int>(stlab::default_executor);
    std::tie(X, Y) = stlab::channel<int>(stlab::default_executor);

    auto b = B | [](int x) { return x * 2; } | X;

    auto y = Y | [](int v) { return v + 2; } | [&](int v) { result = v; };

    B.set_ready();
    Y.set_ready();

    A(20);

    wait_until_done([&] { return result == 42; });

    REQUIRE(result == 42);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_concatenate_channels_with_different_executor") {
    {
        std::atomic_int result{0};

        auto done = _receive[0] |
                    (stlab::executor{stlab::immediate_executor} & [](int x) { return x + 1; }) |
                    [&result](int x) { result = x; };

        _receive[0].set_ready();

        _send[0](42);

        wait_until_done([&] { return result == 43; });

        REQUIRE(result == 43);
    }
    test_reset();
    {
        std::atomic_int result{0};

        auto done = _receive[0] |
                    ([](int x) { return x + 1; } & stlab::executor{stlab::immediate_executor}) |
                    [&result](int x) { result = x; };

        _receive[0].set_ready();

        _send[0](42);

        wait_until_done([&] { return result == 43; });

        REQUIRE(result == 43);
    }
}

namespace {

class main_queue {
    std::deque<stlab::task<void()>> _q;

public:
    auto executor() {
        return [this](auto&& task) { _q.emplace_back(std::forward<decltype(task)>(task)); };
    }

    void run() {
        while (execute_next_task()) {
        }
    }

    auto execute_next_task() -> bool {
        if (!_q.empty()) {
            _q.front()();
            _q.pop_front();
            return true;
        }
        return false;
    }
};

struct echo {
    stlab::process_state_scheduled _state = stlab::await_forever;
    int _result = -1;

    void await(int x) {
        _result = x;
        _state = stlab::yield_immediate;
    }

    auto yield() -> int {
        _state = stlab::await_forever;
        return _result;
    }

    void close() {}

    [[nodiscard]] auto state() const -> const auto& { return _state; }
};

template <typename... T>
struct generator {
    const stlab::process_state_scheduled _state = stlab::yield_immediate;

    int _value = 0;
    std::tuple<T&...> _queues;

    explicit generator(T&... q) : _queues(q...) {}

    template <size_t... I>
    void push_values(int value, std::index_sequence<I...> /*unused*/) {
        (void)std::initializer_list<int>{(std::get<I>(_queues).push(value), 0)...};
    }

    auto yield() -> int {
        push_values(_value, std::make_index_sequence<sizeof...(T)>());
        return _value++;
    }

    void close() {}

    [[nodiscard]] auto state() const -> const auto& { return _state; }
};

void RequireInClosedRange(size_t minValue, size_t test, size_t maxValue) {
    REQUIRE(minValue <= test);
    REQUIRE(test <= maxValue);
}

} // namespace

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_with_2_sized_buffer") {
    main_queue q;

    auto receive = stlab::channel<void>(q.executor());
    std::queue<int> valuesInFlight;

    generator<std::queue<int>> myGenerator(valuesInFlight);
    echo myEcho;

    auto r2 = std::move(receive) | ref(myGenerator) | (stlab::buffer_size{2} & std::ref(myEcho)) |
              [&valuesInFlight](auto x) {
                  REQUIRE(valuesInFlight.front() == x);
                  valuesInFlight.pop();
                  std::cout << x << std::endl;
              };

    r2.set_ready();

    // The first buffer has size 2 and the next process has per default size 1,
    // so there can be max 2 + 1 values in flight
    q.execute_next_task(); // generate value(0)
    REQUIRE(valuesInFlight.size() <= 3U);
    q.execute_next_task(); // await and yield value(0) by echo
    REQUIRE(valuesInFlight.size() <= 3U);

    q.execute_next_task(); // generate value(1)
    REQUIRE(valuesInFlight.size() <= 3U);
    q.execute_next_task(); // generate value(2)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);

    q.execute_next_task(); // print value (0)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // generate value(3)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // await and yield value(1) by echo
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // print value (1)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // await and yield value(2) by echo
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // generate value(4)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // print value (2)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // await and yield value(3) by echo
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // generate value(5)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
    q.execute_next_task(); // print value (3)
    RequireInClosedRange(2U, valuesInFlight.size(), 3U);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_with_3_sized_buffer") {
    main_queue q;

    auto receive = stlab::channel<void>(q.executor());
    std::queue<int> valuesInFlight;

    generator<std::queue<int>> myGenerator(valuesInFlight);
    echo myEcho;

    auto r2 = std::move(receive) | std::ref(myGenerator) |
              (stlab::buffer_size{3} & std::ref(myEcho)) | [&valuesInFlight](auto x) {
                  REQUIRE(valuesInFlight.front() == x);
                  valuesInFlight.pop();
              };

    r2.set_ready();

    // The first buffer has size 3 and the next process has per default size 1,
    // so there can be max 3 + 1 values in flight

    q.execute_next_task(); // generate value(0)
    REQUIRE(valuesInFlight.size() <= 3U);
    q.execute_next_task(); // await and yield value(0) by echo
    REQUIRE(valuesInFlight.size() <= 3U);
    q.execute_next_task(); // generate value(1)
    REQUIRE(valuesInFlight.size() <= 3U);
    q.execute_next_task(); // generate value(2)

    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // print value (0)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // generate value(3)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // generate value(4)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // await and yield value(1) by echo
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // print value (1)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // await and yield value(2) by echo
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // generate value(5)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // print value (2)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // await and yield value(3) by echo
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // generate value(6)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
    q.execute_next_task(); // print value (3)
    RequireInClosedRange(2U, valuesInFlight.size(), 4U);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_with_split_different_sized_buffer") {
    // Here the bigger buffer size must not steer the upstream, but the
    // smaller size
    std::vector<std::pair<size_t, size_t>> const bufferSizes = {
        {1, 2}, {1, 2}, {1, 3}, {3, 1}, {2, 1}};

    for (const auto& bs : bufferSizes) {
        main_queue q;

        std::queue<int> valuesInFlight1;
        std::queue<int> valuesInFlight2;

        generator<std::queue<int>> const myGenerator1(valuesInFlight1);
        generator<std::queue<int>> const myGenerator2(valuesInFlight2);

        auto receive = stlab::channel<void>(q.executor());

        auto g = std::move(receive) |
                 generator<std::queue<int>, std::queue<int>>(valuesInFlight1, valuesInFlight2);

        auto r1 = g | (stlab::buffer_size{bs.first} & echo()) | [&valuesInFlight1](auto x) {
            REQUIRE(valuesInFlight1.front() == x);
            valuesInFlight1.pop();
        };
        auto r2 = g | (stlab::buffer_size{bs.second} & echo()) | [&valuesInFlight2](auto x) {
            REQUIRE(valuesInFlight2.front() == x);
            valuesInFlight2.pop();
        };

        g.set_ready();
        r1.set_ready();
        r2.set_ready();

        // In all cases one buffer has just the size of 1 so the overall number of values
        // in flight can only be 1 + 1

        q.execute_next_task(); // generate value(0)
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // await and yield value(0) by echo1
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // await and yield value(0) by echo2
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // print1 value (0)
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // generate value(1)
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // print2 value (0)
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // await and yield value(1) by echo1
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // await and yield value(1) by echo2
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // print1 value (1)
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
        q.execute_next_task(); // generate value(1)
        q.execute_next_task(); // print2 value (1)
        RequireInClosedRange(0, valuesInFlight1.size(), 2U);
        RequireInClosedRange(0, valuesInFlight2.size(), 2U);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_one_value_different_buffer_sizes") {
    // "int channel one value different buffer sizes");

    for (auto bs : std::vector<std::size_t>{0, 1, 2, 10}) {
        stlab::sender<int> send;
        stlab::receiver<int> receive;
        std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);
        std::atomic_int result{0};

        auto check = receive | (stlab::buffer_size{bs} & [&](int x) { result += x; });

        receive.set_ready();
        send(1);

        while (result < 1) {
            invoke_waiting([] { std::this_thread::sleep_for(std::chrono::microseconds(1)); });
        }

        REQUIRE(result == 1);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_two_values_different_buffer_sizes") {
    // "int channel two values different buffer sizes");

    for (auto bs : std::vector<std::size_t>{0, 1, 2, 10}) {
        stlab::sender<int> send;
        stlab::receiver<int> receive;
        std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);
        std::atomic_int result{0};

        auto check = receive | (stlab::buffer_size{bs} & [&](int x) { result += x; });

        receive.set_ready();
        send(1);
        send(1);

        while (result < 2) {
            invoke_waiting([] { std::this_thread::sleep_for(std::chrono::microseconds(1)); });
        }

        REQUIRE(result == 2);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_many_values_different_buffer_sizes") {
    // "int channel many values different buffer sizes");

    {
        for (auto bs : std::vector<std::size_t>{0, 1, 2, 10}) {
            stlab::sender<int> send;
            stlab::receiver<int> receive;
            std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);
            std::atomic_int result{0};

            auto check = receive | (stlab::buffer_size{bs} & [&](int x) { result += x; });

            receive.set_ready();
            for (auto i = 0; i < 10; ++i) {
                send(1);
            }

            while (result < 10) {
                invoke_waiting([] { std::this_thread::sleep_for(std::chrono::microseconds(1)); });
            }

            REQUIRE(result == 10);
        }
    }
    {
        for (auto bs : std::vector<std::size_t>{0, 1, 2, 10}) {
            stlab::sender<int> send;
            stlab::receiver<int> receive;
            std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);
            std::atomic_int result{0};

            auto check = receive | ([&](int x) { result += x; } & stlab::buffer_size{bs});

            receive.set_ready();
            for (auto i = 0; i < 10; ++i) {
                send(1);
            }

            while (result < 10) {
                invoke_waiting([] { std::this_thread::sleep_for(std::chrono::microseconds(1)); });
            }

            REQUIRE(result == 10);
        }
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "report_channel_broken_when_no_process_is_attached") {
    // "Expect broken channel exception when no process is attached");

    stlab::receiver<int> receive;

    REQUIRE_THROWS_AS((void)(receive | [](int) { return 1; }), stlab::channel_error);
    REQUIRE_THROWS_AS((void)(receive | (stlab::buffer_size{2} & [](int) { return 1; })),
                      stlab::channel_error);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1,
                  "report_channel_broken_when_process_is_already_running") {
    stlab::sender<int> send;
    stlab::receiver<int> receive;
    std::tie(send, receive) = stlab::channel<int>(stlab::default_executor);

    receive.set_ready();

    REQUIRE_THROWS_AS((void)(receive | [](int) { return 1; }), stlab::channel_error);
    REQUIRE_THROWS_AS((void)(receive | (stlab::buffer_size{2} & [](int) { return 1; })),
                      stlab::channel_error);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "sender_receiver_equality_tests") {
    // "running sender equality tests");
    {
        stlab::sender<int> a;
        stlab::sender<int> b;
        stlab::receiver<int> x;
        stlab::receiver<int> y;

        REQUIRE(a == b);
        REQUIRE(!(a != b));

        REQUIRE(x == y);
        REQUIRE(!(x != y));
    }

    {
        stlab::sender<stlab::move_only> a;
        stlab::sender<stlab::move_only> b;
        stlab::receiver<stlab::move_only> x;
        stlab::receiver<stlab::move_only> y;

        REQUIRE(a == b);
        REQUIRE(!(a != b));

        REQUIRE(x == y);
        REQUIRE(!(x != y));
    }

    {
        stlab::sender<int> send;
        stlab::receiver<int> rec;
        std::tie(send, rec) = stlab::channel<int>(stlab::immediate_executor);

        auto a = send;
        auto x = rec;
        REQUIRE(send == a);
        REQUIRE(rec == x);
    }

    {
        stlab::sender<int> a;
        stlab::sender<int> b;
        stlab::receiver<int> x;
        stlab::receiver<int> y;
        std::tie(a, x) = stlab::channel<int>(stlab::immediate_executor);
        std::tie(b, y) = stlab::channel<int>(stlab::immediate_executor);

        REQUIRE(a != b);
        REQUIRE(x != y);
    }
    {
        stlab::sender<stlab::move_only> send;
        stlab::receiver<stlab::move_only> rec;
        std::tie(send, rec) = stlab::channel<stlab::move_only>(stlab::immediate_executor);

        stlab::sender<stlab::move_only> const a;
        REQUIRE(a != send);
    }
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "sender_receiver_swap_tests") {
    {
        stlab::sender<int> a;
        stlab::sender<int> b;
        stlab::receiver<int> x;
        stlab::receiver<int> y;
        std::tie(a, x) = stlab::channel<int>(stlab::immediate_executor);
        std::tie(b, y) = stlab::channel<int>(stlab::immediate_executor);
        int result1(0);
        int result2(0);

        auto v = x | [&result1](int i) { result1 = i; };
        auto w = y | [&result2](int i) { result2 = i; };

        x.set_ready();
        y.set_ready();

        swap(a, b);

        a(1);
        b(2);

        REQUIRE(result2 == 1);
        REQUIRE(result1 == 2);
    }
    {
        stlab::sender<int> a;
        stlab::sender<int> b;
        stlab::receiver<int> x;
        stlab::receiver<int> y;
        std::tie(a, x) = stlab::channel<int>(stlab::immediate_executor);
        std::tie(b, y) = stlab::channel<int>(stlab::immediate_executor);
        int result1(0);
        int result2(0);

        swap(x, y);

        auto v = x | [&result1](int i) { result1 = i; };
        auto w = y | [&result2](int i) { result2 = i; };

        x.set_ready();
        y.set_ready();

        a(1);
        b(2);

        REQUIRE(result2 == 1);
        REQUIRE(result1 == 2);
    }
    {
        stlab::sender<stlab::move_only> a;
        stlab::sender<stlab::move_only> b;
        stlab::receiver<stlab::move_only> x;
        stlab::receiver<stlab::move_only> y;
        std::tie(a, x) = stlab::channel<stlab::move_only>(stlab::immediate_executor);
        std::tie(b, y) = stlab::channel<stlab::move_only>(stlab::immediate_executor);
        int result1(0);
        int result2(0);

        auto v = x | [&result1](stlab::move_only i) { result1 = i.member(); };
        auto w = y | [&result2](stlab::move_only i) { result2 = i.member(); };

        x.set_ready();
        y.set_ready();

        swap(a, b);

        a(1);
        b(2);

        REQUIRE(result2 == 1);
        REQUIRE(result1 == 2);
    }
    {
        stlab::sender<stlab::move_only> a;
        stlab::sender<stlab::move_only> b;
        stlab::receiver<stlab::move_only> x;
        stlab::receiver<stlab::move_only> y;
        std::tie(a, x) = stlab::channel<stlab::move_only>(stlab::immediate_executor);
        std::tie(b, y) = stlab::channel<stlab::move_only>(stlab::immediate_executor);
        int result1(0);
        int result2(0);

        std::swap(x, y);

        auto v = x | [&result1](stlab::move_only i) { result1 = i.member(); };
        auto w = y | [&result2](stlab::move_only i) { result2 = i.member(); };

        x.set_ready();
        y.set_ready();

        a(1);
        b(2);

        REQUIRE(result2 == 1);
        REQUIRE(result1 == 2);
    }
}
