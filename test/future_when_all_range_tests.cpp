/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <array>
#include <atomic>
#include <chrono> // IWYU pragma: keep
#include <cstddef>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/serial_queue.hpp>
#include <stlab/test/model.hpp>
#include <stlab/utility.hpp>

#include "future_test_helper.hpp"

using namespace stlab;
using namespace future_test_helper;

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_void_empty_range") {
    bool check = {false};
    std::vector<stlab::future<void>> emptyFutures;

    sut = when_all(
        make_executor<0>(), [&_check = check]() { _check = true; },
        std::make_pair(emptyFutures.begin(), emptyFutures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE(check);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_empty_range") {
    size_t p = 0;
    std::vector<stlab::future<int>> emptyFutures;

    sut = when_all(
        make_executor<0>(), [&_p = p](const std::vector<int>& v) { _p = v.size(); },
        std::make_pair(emptyFutures.begin(), emptyFutures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_one_element") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 42; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](std::vector<int> v) {
            _p = v.size();
            _r = v[0];
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(1)));
    REQUIRE((r == size_t(42)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_all_delayed") {
    using namespace std::chrono_literals;

    stlab::serial_queue_t const seriel_queue_1{stlab::default_executor};
    stlab::serial_queue_t const seriel_queue_2{stlab::default_executor};
    stlab::serial_queue_t const seriel_queue_3{stlab::default_executor};

    std::vector<stlab::future<void>> test_futures;
    test_futures.emplace_back(stlab::async(seriel_queue_1.executor(), [] {
        invoke_waiting([] { std::this_thread::sleep_for(0.1s); });
    }));

    test_futures.emplace_back(stlab::async(seriel_queue_2.executor(), [] {
        invoke_waiting([] { std::this_thread::sleep_for(0.1s); });
    }));

    test_futures.emplace_back(stlab::async(seriel_queue_3.executor(), [] {
        invoke_waiting([] { std::this_thread::sleep_for(0.1s); });
    }));
    bool done{false};
    auto done_future = stlab::when_all(
        stlab::default_executor, [&done] { done = true; },
        std::make_pair(test_futures.begin(), test_futures.end()));

    stlab::await(std::move(done_future));

    REQUIRE(done);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_many_elements") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 1; }));
    futures.push_back(async(make_executor<0>(), [] { return 2; }));
    futures.push_back(async(make_executor<0>(), [] { return 3; }));
    futures.push_back(async(make_executor<0>(), [] { return 5; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE((r == size_t(1 + 2 + 3 + 5)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_many_elements_and_immediate_continuation") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.reserve(1000);
    for (auto i = 0; i < 1000; ++i) {
        futures.push_back(async(make_executor<0>(), [] { return 1; }));
    }

    sut = when_all(
        immediate_executor,
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE((p == size_t(1000)));
    REQUIRE((r == size_t(1000)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1000);
}

/*
       /  F1  \
      / / F2 \ \
start           sut
      \ \ F3 / /
       \  F4  /
*/
TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_diamond_formation_elements") {
    std::array v{0, 0, 0, 0};
    int r = 0;
    auto start = async(make_executor<0>(), [] { return 4711; });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [&_p = v[1]](auto x) { _p = x + 2; });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_r = r, &v]() {
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(std::move(sut));

    REQUIRE(r == 4711 + 1 + 4711 + 2 + 4711 + 3 + 4711 + 5);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 5);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_empty_range") {
    std::vector<stlab::future<int>> emptyFutures;

    sut = when_all(
        make_executor<0>(), [](const std::vector<int>& v) { return static_cast<int>(v.size()); },
        std::make_pair(emptyFutures.begin(), emptyFutures.end()));
    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((*sut.get_try() == 0));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_range_with_one_element") {
    size_t p = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 42; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p](std::vector<int> v) {
            _p = v.size();
            return v[0];
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((p == size_t(1)));
    REQUIRE((*sut.get_try() == 42));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_range_with_many_elements") {
    size_t p = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), [] { return 1; }));
    futures.push_back(async(make_executor<0>(), [] { return 2; }));
    futures.push_back(async(make_executor<0>(), [] { return 3; }));
    futures.push_back(async(make_executor<0>(), [] { return 5; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p](const std::vector<int>& v) {
            _p = v.size();
            auto r = 0;
            for (auto i : v) {
                r += i;
            }
            return r;
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE((*sut.get_try() == 1 + 2 + 3 + 5));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

/*
       /  F1  \
      / / F2 \ \
start           sut
      \ \ F3 / /
       \  F4  /
*/
TEST_CASE_FIXTURE(test_fixture<int>, "future_when_all_int_range_with_diamond_formation_elements") {
    size_t p = 0;
    auto start = async(make_executor<0>(), [] { return 4711; });
    std::vector<stlab::future<int>> futures(4);
    futures[0] = start.then(make_executor<0>(), [](auto x) { return x + 1; });
    futures[1] = start.then(make_executor<0>(), [](auto x) { return x + 2; });
    futures[2] = start.then(make_executor<0>(), [](auto x) { return x + 3; });
    futures[3] = start.then(make_executor<0>(), [](auto x) { return x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_p = p](const std::vector<int>& v) {
            _p = v.size();
            auto r = 0;
            for (auto i : v) {
                r += i;
            }
            return r;
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    wait_until_future_completed(copy(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE((*sut.get_try() == 4711 + 1 + 4711 + 2 + 4711 + 3 + 4711 + 5));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<stlab::move_only>, "future_when_all_move_range_with_many_elements") {
    size_t p = 0;
    std::vector<stlab::future<stlab::move_only>> futures;
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{1}; }));
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{2}; }));
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{3}; }));
    futures.push_back(async(make_executor<0>(), [] { return stlab::move_only{5}; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p](const std::vector<stlab::move_only>& v) {
            _p = v.size();
            auto r = 0;
            for (const auto& i : v) {
                r += i.member();
            }
            return stlab::move_only{r};
        },
        std::make_pair(futures.begin(), futures.end()));

    check_valid_future(sut);
    auto result = await(std::move(sut));

    REQUIRE((p == size_t(4)));
    REQUIRE(result.member() == 1 + 2 + 3 + 5);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 4);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_range_with_mutable_task") {
    struct mutable_int {
        int i = 0;
        auto operator()() {
            ++i;
            return i;
        }
    };
    mutable_int func1;
    mutable_int func2;
    std::vector<future<mutable_int>> futures{
        async(stlab::default_executor,
              [func = func1]() mutable {
                  func();
                  return func;
              }),
        async(stlab::default_executor, [func = func2]() mutable {
            func();
            return func;
        })};
    auto result = when_all(
        stlab::default_executor,
        [](auto result_vec) {
            return std::accumulate(result_vec.begin(), result_vec.end(), 0,
                                   [](int sum, auto f) { return sum + f(); });
        },
        std::make_pair(futures.begin(), futures.end()));

    REQUIRE(stlab::await(std::move(result)) == 4);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_range_with_mutable_void_task") {
    std::atomic_int check{0};
    struct mutable_int {
        std::atomic_int& _check;
        int i = 0;
        void operator()() {
            ++i;
            ++_check;
        }
    };

    mutable_int func1{check};
    mutable_int func2{check};
    std::vector<future<void>> futures{
        async(stlab::default_executor, [func = func1]() mutable { func(); }),
        async(stlab::default_executor, [func = func2]() mutable { func(); })};

    future<void> result = when_all(
        stlab::default_executor, [_func = mutable_int{check}]() mutable { _func(); },
        std::make_pair(futures.begin(), futures.end()));

    stlab::await(std::move(result));

    REQUIRE(check == 3);
}

// ----------------------------------------------------------------------------
//                             Error cases
// ----------------------------------------------------------------------------

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_one_element") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            _r = v[0];
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE((p == size_t(0)));
    REQUIRE((r == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_many_elements_one_failing") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), [] { return 2; }));
    futures.push_back(async(make_executor<0>(), [] { return 3; }));
    futures.push_back(async(make_executor<0>(), [] { return 5; }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE((p == size_t(0)));
    REQUIRE((r == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_when_all_void_range_with_many_elements_all_failing") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));

    sut = when_all(
        make_executor<1>(),
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE((p == size_t(0)));
    REQUIRE((r == size_t(0)));
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

/*
       /  F1  \
      / / F2 \ \
start           sut
      \ \ F3 / /
       \  F4  /
*/
TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_diamond_formation_elements_start_failing") {
    std::array v{0, 0, 0, 0};
    int r = 0;
    auto start = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [&_p = v[1]](auto x) { _p = x + 2; });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_r = r, &v]() {
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0);
    for (auto d : v) {
        REQUIRE(d == 0);
    }
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_when_all_void_range_with_diamond_formation_elements_one_parallel_failing") {
    std::array v{0, 0, 0, 0};
    int r = 0;
    auto start = async(make_executor<0>(), []() -> int { return 42; });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [](auto) { throw test_exception("failure"); });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(),
        [&_r = r, &v]() {
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_when_all_void_range_with_diamond_formation_elements_join_failing") {
    std::array v{0, 0, 0, 0};
    int const r = 0;
    auto start = async(make_executor<0>(), []() -> int { return 42; });
    std::vector<stlab::future<void>> futures(4);
    futures[0] = start.then(make_executor<0>(), [&_p = v[0]](auto x) { _p = x + 1; });
    futures[1] = start.then(make_executor<0>(), [&_p = v[1]](auto x) { _p = x + 2; });
    futures[2] = start.then(make_executor<0>(), [&_p = v[2]](auto x) { _p = x + 3; });
    futures[3] = start.then(make_executor<0>(), [&_p = v[3]](auto x) { _p = x + 5; });

    sut = when_all(
        make_executor<1>(), []() { throw test_exception("failure"); },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0);
    REQUIRE(custom_scheduler<0>::usage_counter() >= 1);
    REQUIRE(custom_scheduler<1>::usage_counter() >= 1);
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_when_all_void_range_with_one_fails_ofmany_elements_and_immediate_continuation") {
    size_t p = 0;
    size_t r = 0;
    std::vector<stlab::future<int>> futures;
    futures.reserve(500);
    for (auto i = 0; i < 500; ++i) {
        futures.push_back(async(make_executor<0>(), [] { return 1; }));
    }
    futures.push_back(async(make_executor<0>(), []() -> int { throw test_exception("failure"); }));

    for (auto i = 0; i < 499; ++i) {
        futures.push_back(async(make_executor<0>(), [] { return 1; }));
    }

    sut = when_all(
        immediate_executor,
        [&_p = p, &_r = r](const std::vector<int>& v) {
            _p = v.size();
            for (auto i : v) {
                _r += i;
            }
        },
        std::make_pair(futures.begin(), futures.end()));

    wait_until_future_fails<test_exception>(copy(sut));

    check_failure<test_exception>(sut, "failure");
    REQUIRE(r == 0u);
}
