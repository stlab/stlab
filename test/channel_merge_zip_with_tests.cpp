/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <atomic>
#include <string>
#include <tuple>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/channel.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>

#include "channel_test_helper.hpp"

using channel_test_fixture_int_1 = channel_test_helper::channel_test_fixture<int, 1>;

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_zip_with_channel_void_functor_one_value") {
    std::atomic_int result{0};

    auto check = zip_with(stlab::default_executor, [&](int x) { result = x; }, _receive[0]);

    _receive[0].set_ready();
    _send[0](1);

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 1);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1,
                  "int_zip_with_channel_void_functor_one_value_async") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(stlab::default_executor, [&](int x) { result = x; }, _receive[0]);

    _receive[0].set_ready();
    auto f = stlab::async(stlab::default_executor, [_sender = _send[0]] { _sender(1); });

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 1);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1,
                  "int_zip_width_channel_void_functor_many_values") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(stlab::default_executor, [&](int x) { result += x; }, _receive[0]);

    _receive[0].set_ready();
    for (auto i = 1; i <= 100; ++i) {
        _send[0](i);
    }

    auto expected = 100 * (100 + 1) / 2;

    wait_until_done([&]() { return result >= expected; });

    REQUIRE(result == expected);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1,
                  "int_zip_width_channel_void_functor_many_values_async") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(stlab::default_executor, [&](int x) { result += x; }, _receive[0]);

    _receive[0].set_ready();
    std::vector<stlab::future<void>> f(100);
    for (auto i = 1; i <= 100; ++i) {
        f.push_back(
            stlab::async(stlab::default_executor, [_sender = _send[0], _i = i] { _sender(_i); }));
    }

    auto expected = 100 * (100 + 1) / 2;
    wait_until_done([&]() { return result >= expected; });

    REQUIRE(result == expected);
}

using channel_test_fixture_int_2 = channel_test_helper::channel_test_fixture<int, 2>;

TEST_CASE_FIXTURE(channel_test_fixture_int_2,
                  "int_zip_width_channel_same_type_void_functor_one_value") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(
        stlab::default_executor, [&](int x, int y) { result += 2 * x + 3 * y; }, _receive[0],
        _receive[1]);

    _receive[0].set_ready();
    _receive[1].set_ready();
    _send[0](2);
    _send[1](3);

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 13);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_2,
                  "int_zip_width_channel_same_type_void_functor_one_value_async") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(
        stlab::default_executor, [&](int x, int y) { result += 2 * x + 3 * y; }, _receive[0],
        _receive[1]);

    _receive[0].set_ready();
    _receive[1].set_ready();
    auto f = stlab::async(stlab::default_executor,
                          [_send1 = _send[0], &_send2 = _send[1]] { // one copy,one reference
                              _send1(2);
                              _send2(3);
                          });

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 13);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_2,
                  "int_zip_width_channel_same_type_void_functor_many_values") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(
        stlab::default_executor, [&](int x, int y) { result += 2 * x + 3 * y; }, _receive[0],
        _receive[1]);

    _receive[0].set_ready();
    _receive[1].set_ready();
    for (auto i = 0; i < 10; i++) {
        _send[0](i);
        _send[1](i + 1);
    }

    wait_until_done([&]() { return result >= 255; });

    const auto expected_result =
        3 + 2 + 6 + 4 + 9 + 6 + 12 + 8 + 15 + 10 + 18 + 12 + 21 + 14 + 24 + 16 + 27 + 18 + 30;
    REQUIRE(result == expected_result);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_2,
                  "int_zip_width_channel_same_type_void_functor_many_values_async") {
    std::atomic_int result{0};

    auto check = zip_with(
        stlab::default_executor, [&](int x, int y) { result += 2 * x + 3 * y; }, _receive[0],
        _receive[1]);

    _receive[0].set_ready();
    _receive[1].set_ready();
    std::vector<stlab::future<void>> f(20);
    for (auto i = 0; i < 10; i++) {
        f.push_back(
            stlab::async(stlab::default_executor, [_send1 = _send[0], _i = i] { _send1(_i); }));
        f.push_back(stlab::async(stlab::default_executor,
                                 [&_send2 = _send[1], _i = i] { _send2(_i + 1); }));
    }

    const auto expected =
        3 + 2 + 6 + 4 + 9 + 6 + 12 + 8 + 15 + 10 + 18 + 12 + 21 + 14 + 24 + 16 + 27 + 18 + 30;
    wait_until_done([&]() { return result >= expected; });

    REQUIRE(result == expected);
}

using channel_test_fixture_int_5 = channel_test_helper::channel_test_fixture<int, 5>;
TEST_CASE_FIXTURE(channel_test_fixture_int_5,
                  "int_zip_width_channel_same_type_void_functor") {
    std::atomic_int result{0};

    auto check = zip_with(
        stlab::default_executor,
        [&](int v, int w, int x, int y, int z) { result += 2 * v + 3 * w + 4 * x + 5 * y + 6 * z; },
        _receive[0], _receive[1], _receive[2], _receive[3], _receive[4]);

    for (auto& r : _receive) {
        r.set_ready();
    }

    auto i = 2;
    for (auto& s : _send) {
        s(i++);
    }

    const auto expectation = 4 + 9 + 16 + 25 + 36;
    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == expectation);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_5,
                  "int_zip_width_channel_same_type_void_functor_async") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(
        stlab::default_executor,
        [&](int v, int w, int x, int y, int z) { result += 2 * v + 3 * w + 4 * x + 5 * y + 6 * z; },
        _receive[0], _receive[1], _receive[2], _receive[3], _receive[4]);

    for (auto& r : _receive) {
        r.set_ready();
    }

    std::vector<stlab::future<void>> f(5);
    for (auto i = 0; i < 5; i++) {
        f.push_back(
            stlab::async(stlab::default_executor, [_send = _send[i], _i = i] { _send(_i + 2); }));
    }

    wait_until_done([&]() { return result != 0; });
    auto expected = 4 + 9 + 16 + 25 + 36;

    REQUIRE(result == expected);
}

using channel_types_test_fixture_int_string =
    channel_test_helper::channel_types_test_fixture<int, std::string>;

TEST_CASE_FIXTURE(channel_types_test_fixture_int_string,
                  "int_zip_width_channel_different_type_void_functor") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(
        stlab::default_executor,
        [&](int, const std::string& y) { result += 2 + static_cast<int>(y.size()); }, receive<0>(),
        receive<1>());

    receive<0>().set_ready();
    receive<1>().set_ready();
    send<0>()(2);
    send<1>()(std::string("Foo"));

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 5);
}

TEST_CASE_FIXTURE(channel_types_test_fixture_int_string,
                  "int_zip_width_channel_2_join_different_type_void_functor_async") {
    std::atomic_int result{0};

    auto check = stlab::zip_with(
        stlab::default_executor,
        [&](int x, const std::string& y) { result += x + static_cast<int>(y.size()); },
        receive<0>(), receive<1>());

    receive<0>().set_ready();
    receive<1>().set_ready();
    auto f1 = stlab::async(stlab::default_executor, [_send = send<0>()] { _send(2); });
    auto f2 = stlab::async(stlab::default_executor, [_send = send<1>()] { _send("Foo"); });

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 5);
}

TEST_CASE_FIXTURE(channel_types_test_fixture_int_string,
                  "int_zip_with_channel_2_different_type_void_functor_async") {
    std::atomic_int result{0};

    auto check = stlab::zip(stlab::default_executor, receive<0>(), receive<1>()) |
                 [&](std::tuple<int, std::string> v) {
                     result += std::get<0>(v) + static_cast<int>(std::get<1>(v).size());
                 };

    receive<0>().set_ready();
    receive<1>().set_ready();
    auto f1 = stlab::async(stlab::default_executor, [_send = send<0>()] { _send(2); });
    auto f2 = stlab::async(stlab::default_executor, [_send = send<1>()] { _send("Foo"); });

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 5);
}
