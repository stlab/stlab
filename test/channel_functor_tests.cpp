/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <atomic>

#include <doctest/doctest.h>

#include <stlab/concurrency/channel.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/test/model.hpp>

#include <vector>

#include "channel_test_helper.hpp"

using channel_test_fixture_int_1 = channel_test_helper::channel_test_fixture<int, 1>;

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_void_functor_one_value") {
    std::atomic_int result{0};

    auto check = _receive[0] | [&](int x) { result += x; };

    _receive[0].set_ready();
    _send[0](1);

    wait_until_done([&]() { return result != 0; });

    REQUIRE(result == 1);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_void_functor_two_values") {
    std::atomic_int result{0};

    auto check = _receive[0] | [&](int x) { result += x; };

    _receive[0].set_ready();
    _send[0](1);
    _send[0](2);

    wait_until_done([&]() { return result == 3; });
    REQUIRE(result == 3);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_void_functor_many_values") {
    std::atomic_int result{0};

    auto check = _receive[0] | [&](int x) { result += x; };

    _receive[0].set_ready();
    for (int i = 0; i < 10; ++i) {
        _send[0](1);
    }

    wait_until_done([&]() { return result == 10; });
    REQUIRE(result == 10);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_split_void_functor") {
    std::atomic_int result1{0};
    std::atomic_int result2{0};

    auto check1 = _receive[0] | [&_result = result1](int x) { _result += x; };
    auto check2 = _receive[0] | [&_result = result2](int x) { _result += x; };

    _receive[0].set_ready();
    for (int i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&]() { return result1 == 45 && result2 == 45; });

    REQUIRE(result1 == 45);
    REQUIRE(result2 == 45);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_int_functor") {
    std::atomic_int result{0};

    auto check = _receive[0] | [](int x) { return x + x; } | [&](int x) { result += x; };

    _receive[0].set_ready();
    for (int i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&]() { return result == 90; });
    REQUIRE(result == 90);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_split_int_functor") {
    std::atomic_int result1{0};
    std::atomic_int result2{0};

    auto check1 =
        _receive[0] | [](int x) { return x + x; } | [&_result = result1](int x) { _result += x; };
    auto check2 =
        _receive[0] | [](int x) { return x + x; } | [&_result = result2](int x) { _result += x; };

    _receive[0].set_ready();
    for (int i = 0; i < 10; ++i) {
        _send[0](i);
    }

    wait_until_done([&] { return result1 == 90 && result2 == 90; });

    REQUIRE(result1 == 90);
    REQUIRE(result2 == 90);
}

TEST_CASE_FIXTURE(channel_test_fixture_int_1, "int_channel_split_int_functor_async") {
    std::vector<stlab::future<void>> inputs;
    inputs.reserve(10);
    for (auto i = 0; i < 10; ++i) {
        inputs.emplace_back(
            stlab::async(stlab::default_executor, [&_send = _send, i]() { _send[0](i); }));
    }

    std::atomic_int result1{0};
    std::atomic_int result2{0};

    auto check1 =
        _receive[0] | [](int x) { return x + x; } | [&_result = result1](int x) { _result += x; };
    auto check2 =
        _receive[0] | [](int x) { return x + x; } | [&_result = result2](int x) { _result += x; };

    _receive[0].set_ready();

    wait_until_done([&] { return result1 == 90 && result2 == 90; });

    REQUIRE(result1 == 90);
    REQUIRE(result2 == 90);
}

using channel_test_fixture_move_only_1 =
    channel_test_helper::channel_test_fixture<stlab::move_only, 1>;

TEST_CASE_FIXTURE(channel_test_fixture_move_only_1, "move_only_int_channel_void_functor") {
    std::atomic_int result{0};

    auto check = _receive[0] | [&](stlab::move_only x) { result += x.member(); };

    _receive[0].set_ready();
    for (int i = 0; i < 10; ++i) {
        _send[0](stlab::move_only(1));
    }

    wait_until_done([&]() { return result == 10; });
    REQUIRE(result == 10);
}

TEST_CASE_FIXTURE(channel_test_fixture_move_only_1, "move_only_int_channel_void_functor_async") {
    std::atomic_int result{0};

    auto check = _receive[0] | [&](stlab::move_only x) { result += x.member(); };

    _receive[0].set_ready();
    std::vector<stlab::future<void>> f;
    f.reserve(10);
    for (int i = 0; i < 10; ++i) {
        f.push_back(stlab::async(stlab::default_executor,
                                 [&_send = _send[0]] { _send(stlab::move_only(1)); }));
    }

    wait_until_done([&]() { return result == 10; });
    REQUIRE(result == 10);
}

TEST_CASE_FIXTURE(channel_test_fixture_move_only_1, "move_only_int_channel_int_functor") {
    std::atomic_int result{0};

    auto check = _receive[0] | [](stlab::move_only x) { return stlab::move_only(2 * x.member()); } |
                 [&](stlab::move_only x) { result += x.member(); };

    _receive[0].set_ready();
    for (int i = 0; i < 10; ++i) {
        _send[0](stlab::move_only(1));
    }

    wait_until_done([&]() { return result >= 20; });

    REQUIRE(result == 20);
}

TEST_CASE_FIXTURE(channel_test_fixture_move_only_1, "move_only_int_channel_int_functor_async") {
    std::atomic_int result{0};

    auto check = _receive[0] | [](stlab::move_only x) { return stlab::move_only(2 * x.member()); } |
                 [&](stlab::move_only x) { result += x.member(); };

    _receive[0].set_ready();
    std::vector<stlab::future<void>> f;
    f.reserve(10);
    for (int i = 0; i < 10; ++i) {
        f.push_back(stlab::async(stlab::default_executor,
                                 [&_send = _send[0]] { _send(stlab::move_only(1)); }));
    }

    wait_until_done([&]() { return result >= 20; });

    REQUIRE(result == 20);
}
