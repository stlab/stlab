/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <exception>
#include <mutex>
#include <utility>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/executor_base.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/test/model.hpp>
#include <stlab/utility.hpp>

#include "future_test_helper.hpp"

using namespace std;
using namespace stlab;
using namespace future_test_helper;

namespace {
int get_c0() { return custom_scheduler<0>::usage_counter(); }
int get_c1() { return custom_scheduler<1>::usage_counter(); }
} // namespace

// ----------------------------------------------------------------------------
//                                  void
// ----------------------------------------------------------------------------

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_recover_failure_before_recover_initialized_on_rvalue") {
    /*
    combining the tests as in future_then_tests is not possible because of a bug in gcc

    using task_t = function<void(future<void>)>;
    using op_t = future<void>(future<void>::*)(task_t&&)&&;

    op_t ops[] = {static_cast<op_t>(&future<void>::recover<task_t>),
                  static_cast<op_t>(&future<void>::operator^<task_t>)};

    for (const auto& op : ops)
    */
    {
        custom_scheduler<0>::reset();
        auto error = false;
        sut = async(make_executor<0>(), [&_error = error]() -> void {
                  _error = true;
                  throw test_exception("failure");
              }).recover([](future<void> failedFuture) {
            if (failedFuture.exception()) {
                check_failure<test_exception>(failedFuture, "failure");
            }
        });
        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() >= 2);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        sut = (async(make_executor<0>(),
                     [&_error = error]() -> void {
                         _error = true;
                         throw test_exception("failure");
                     }) ^
               [](future<void> failedFuture) {
                   if (failedFuture.exception()) {
                       check_failure<test_exception>(failedFuture, "failure");
                   }
               });
        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() >= 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_recover_failure_before_recover_initialized_on_lvalue") {
    //"running future recover, failure before recover initialized on l-value");

    {
        custom_scheduler<0>::reset();
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error] {
            _error = true;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(interim));

        sut = interim.recover(
            [](auto failedFuture) { check_failure<test_exception>(failedFuture, "failure"); });

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 2);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error] {
            _error = true;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(interim));

        sut = interim ^
              [](auto failedFuture) { check_failure<test_exception>(failedFuture, "failure"); };

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 2);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_recover_failure_before_recover_initialized_with_custom_scheduler_on_rvalue") {
    {
        auto error = false;

        sut = async(make_executor<0>(), [&_error = error] {
                  _error = true;
                  throw test_exception("failure");
              }).recover(make_executor<1>(), [](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
        });

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;

        sut = async(make_executor<0>(),
                    [&_error = error] {
                        _error = true;
                        throw test_exception("failure");
                    }) ^
              (executor{make_executor<1>()} &
               [](auto failedFuture) { check_failure<test_exception>(failedFuture, "failure"); });

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_recover_failure_before_recover_initialized_with_custom_scheduler_on_lvalue") {
    {
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error] {
            _error = true;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(interim));

        sut = interim.recover(make_executor<1>(), [](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
        });

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error] {
            _error = true;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(interim));

        sut = interim ^ (executor{make_executor<1>()} & [](auto failedFuture) {
                  check_failure<test_exception>(failedFuture, "failure");
              });

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_recover_failure_after_recover_initialized_on_rvalue") {
    //"running future recover, failure after recover initialized on r-value");

    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            sut = async(make_executor<0>(), [&_error = error, &_block = block] {
                      lock_t const lock(_block);
                      _error = true;
                      throw test_exception("failure");
                  }).recover([](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
            });
        }

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() >= 2);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error] {
            _error = true;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(interim));

        sut = interim ^
              [](auto failedFuture) { check_failure<test_exception>(failedFuture, "failure"); };

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 2);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>,
                  "future_recover_failure_after_recover_initialized_on_lvalue") {
    //"running future recover, failure after recover initialized on l-value");

    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            auto interim = async(make_executor<0>(), [&_error = error, &_block = block] {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim.recover(
                [](auto failedFuture) { check_failure<test_exception>(failedFuture, "failure"); });
        }

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() >= 2);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            auto interim = async(make_executor<0>(), [&_error = error, &_block = block] {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim ^
                  [](auto failedFuture) { check_failure<test_exception>(failedFuture, "failure"); };
        }

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() >= 2);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_recover_failure_after_recover_initialized_with_custom_scheduler_on_rvalue") {
    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            sut = async(make_executor<0>(), [&_error = error, &_block = block] {
                      lock_t const lock(_block);
                      _error = true;
                      throw test_exception("failure");
                  }).recover(make_executor<1>(), [](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
            });
        }

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            sut = async(make_executor<0>(),
                        [&_error = error, &_block = block] {
                            lock_t const lock(_block);
                            _error = true;
                            throw test_exception("failure");
                        }) ^
                  (executor{make_executor<1>()} & [](auto failedFuture) {
                      check_failure<test_exception>(failedFuture, "failure");
                  });
        }

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<void>,
    "future_recover_failure_after_recover_initialized_with_custom_scheduler_on_lvalue") {
    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            auto interim = async(make_executor<0>(), [&_error = error, &_block = block] {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim.recover(make_executor<1>(), [](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
            });
        }

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            auto interim = async(make_executor<0>(), [&_error = error, &_block = block] {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim ^ (executor{make_executor<1>()} & [](auto failedFuture) {
                      check_failure<test_exception>(failedFuture, "failure");
                  });
        }

        wait_until_future_completed(std::move(sut));

        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<void>, "future_recover_failure_during_when_all_on_lvalue") {
    //"running future recover while failed when_all on l-value");

    {
        custom_scheduler<0>::reset();
        int result{0};
        auto f1 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
        auto f2 = async(make_executor<1>(), [] { return 42; });

        sut = when_all(
                  make_executor<0>(), [](int x, int y) { return x + y; }, f1, f2)
                  .recover([](const auto& error) {
                      if (error.exception()) {
                          return 815;
                      }
                      return 0;
                  })
                  .then([&](int x) { result = x; });

        wait_until_future_completed(std::move(sut));
        REQUIRE(result == 815);
    }
    {
        custom_scheduler<0>::reset();
        int result{0};
        auto f1 = async(make_executor<0>(), []() -> int { throw test_exception("failure"); });
        auto f2 = async(make_executor<1>(), [] { return 42; });

        sut = (when_all(
                   make_executor<0>(), [](int x, int y) { return x + y; }, f1, f2) ^
               [](const auto& error) {
                   if (error.exception()) {
                       return 815;
                   }
                   return 0;
               }) |
              [&](int x) { result = x; };

        wait_until_future_completed(std::move(sut));
        REQUIRE(result == 815);
    }
}

// ----------------------------------------------------------------------------
//                             Copyable Values
// ----------------------------------------------------------------------------

TEST_CASE_FIXTURE(
    test_fixture<int>,
    "future_recover_int_simple_recover_failure_before_recover_initialized_on_rvalue") {
    //"running future int recover, failure before recover initialized on r-value");

    {
        custom_scheduler<0>::reset();
        auto error = false;

        sut = async(make_executor<0>(), [&_error = error]() -> int {
                  _error = true;
                  throw test_exception("failure");
              }).recover([](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
            return 42;
        });

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;

        sut = async(make_executor<0>(),
                    [&_error = error]() -> int {
                        _error = true;
                        throw test_exception("failure");
                    }) ^
              [](auto failedFuture) {
                  check_failure<test_exception>(failedFuture, "failure");
                  return 42;
              };

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<int>,
    "future_recover_int_simple_recover_failure_before_recover_initialized_on_lvalue") {
    //"running future int recover, failure before recover initialized on l-value");

    {
        custom_scheduler<0>::reset();
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error]() -> int {
            _error = true;
            throw test_exception("failure");
        });

        sut = interim.recover([](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
            return 42;
        });

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error]() -> int {
            _error = true;
            throw test_exception("failure");
        });

        sut = interim ^ [](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
            return 42;
        };

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>,
                  "future_recover_int_simple_recover_failure_after_recover_initialized_on_rvalue") {
    //"running future int recover, failure after recover initialized on r-value");

    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);

            sut = async(make_executor<0>(), [&_error = error, &_block = block]() -> int {
                      lock_t const lock(_block);
                      _error = true;
                      throw test_exception("failure");
                  }).recover([](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
                return 42;
            });
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);

            sut = async(make_executor<0>(),
                        [&_error = error, &_block = block]() -> int {
                            lock_t const lock(_block);
                            _error = true;
                            throw test_exception("failure");
                        }) ^
                  [](auto failedFuture) {
                      check_failure<test_exception>(failedFuture, "failure");
                      return 42;
                  };
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>,
                  "future_recover_int_simple_recover_failure_after_recover_initialized_on_lvalue") {
    //"running future int recover, failure after recover initialized on l-value");

    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);

            auto interim = async(make_executor<0>(), [&_error = error, &_block = block]() -> int {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim.recover([](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
                return 42;
            });
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);

            auto interim = async(make_executor<0>(), [&_error = error, &_block = block]() -> int {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim ^ [](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
                return 42;
            };
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<int>,
    "future_recover_int_simple_recover_failure_before_recover_initialized_with_custom_scheduler_on_rvalue") {
    {
        auto error = false;

        sut = async(make_executor<0>(), [&_error = error]() -> int {
                  _error = true;
                  throw test_exception("failure");
              }).recover(make_executor<1>(), [](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
            return 42;
        });

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;

        sut = async(make_executor<0>(),
                    [&_error = error]() -> int {
                        _error = true;
                        throw test_exception("failure");
                    }) ^
              (executor{make_executor<1>()} & [](auto failedFuture) {
                  check_failure<test_exception>(failedFuture, "failure");
                  return 42;
              });

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<int>,
    "future_recover_int_simple_recover_failure_before_recover_initialized_with_custom_scheduler_on_lvalue") {
    {
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error]() -> int {
            _error = true;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(interim));

        sut = interim.recover(make_executor<1>(), [](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
            return 42;
        });

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;
        auto interim = async(make_executor<0>(), [&_error = error]() -> int {
            _error = true;
            throw test_exception("failure");
        });

        wait_until_future_fails<test_exception>(copy(interim));

        sut = interim ^ (executor{make_executor<1>()} & [](auto failedFuture) {
                  check_failure<test_exception>(failedFuture, "failure");
                  return 42;
              });

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<int>,
    "future_recover_int_simple_recover_failure_after_recover_initialized_with_custom_scheduler_on_rvalue") {
    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);

            sut = async(make_executor<0>(), [&_error = error, &_block = block]() -> int {
                      lock_t const lock(_block);
                      _error = true;
                      throw test_exception("failure");
                  }).recover(make_executor<1>(), [](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
                return 42;
            });
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);

            sut = async(make_executor<0>(),
                        [&_error = error, &_block = block]() -> int {
                            lock_t const lock(_block);
                            _error = true;
                            throw test_exception("failure");
                        }) ^
                  (executor{make_executor<1>()} & [](auto failedFuture) {
                      check_failure<test_exception>(failedFuture, "failure");
                      return 42;
                  });
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<int>,
    "future_recover_int_simple_recover_failure_after_recover_initialized_with_custom_scheduler_on_lvalue") {
    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            auto interim = async(make_executor<0>(), [&_error = error, &_block = block]() -> int {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim.recover(make_executor<1>(), [](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
                return 42;
            });
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;
        mutex block;

        {
            lock_t const hold(block);
            auto interim = async(make_executor<0>(), [&_error = error, &_block = block]() -> int {
                lock_t const lock(_block);
                _error = true;
                throw test_exception("failure");
            });

            sut = interim ^ (executor{make_executor<1>()} & [](auto failedFuture) {
                      check_failure<test_exception>(failedFuture, "failure");
                      return 42;
                  });
        }

        auto result = await(std::move(sut));

        REQUIRE(result == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<int>, "future_recover_int_with_broken_promise") {
    //"running future int recover with broken promise");

    {
        auto check{false};
        sut = [&check]() {
            auto p{package<int(int)>(immediate_executor, [](int x) { return x; })};
            return p.second.recover([&check](const auto& f) {
                check = true;
                try {
                    return *f.get_try();
                } catch (const exception&) {
                    throw;
                }
            });
        }();

        check_failure<future_error>(sut, "broken promise");
        REQUIRE(check);
    }
    {
        auto check{false};
        sut = [&check]() {
            auto p{package<int(int)>(immediate_executor, [](int x) { return x; })};
            return p.second ^ [&check](const auto& f) {
                check = true;
                try {
                    return *f.get_try();
                } catch (const exception&) {
                    throw;
                }
            };
        }();

        check_failure<future_error>(sut, "broken promise");
        REQUIRE(check);
    }
}

// ----------------------------------------------------------------------------
//                             Move-only values
// ----------------------------------------------------------------------------

TEST_CASE_FIXTURE(
    test_fixture<move_only>,
    "future_recover_move_only_type_recover_failure_before_recover_initialized_on_rvalue") {
    {
        custom_scheduler<0>::reset();
        auto error = false;

        sut = async(make_executor<0>(), [&_error = error]() -> move_only {
                  _error = true;
                  throw test_exception("failure");
              }).recover([](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
            return move_only(42);
        });

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;

        sut = async(make_executor<0>(),
                    [&_error = error]() -> move_only {
                        _error = true;
                        throw test_exception("failure");
                    }) ^
              [](auto failedFuture) {
                  check_failure<test_exception>(failedFuture, "failure");
                  return move_only(42);
              };

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
    }
}

TEST_CASE_FIXTURE(test_fixture<move_only>,
                  "future_recover_move_only_types_recover_failure_after_recover_initialized") {
    //"running future move only type recover, failure after recover initialized");

    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;
        {
            lock_t const hold(block);

            sut = async(make_executor<0>(), [&_error = error, &_block = block]() -> move_only {
                      lock_t const lock(_block);
                      _error = true;
                      throw test_exception("failure");
                  }).recover([](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
                return move_only(42);
            });
        }

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
    }
    {
        custom_scheduler<0>::reset();
        auto error = false;
        mutex block;
        {
            lock_t const hold(block);

            sut = async(make_executor<0>(),
                        [&_error = error, &_block = block]() -> move_only {
                            lock_t const lock(_block);
                            _error = true;
                            throw test_exception("failure");
                        }) ^
                  [](auto failedFuture) {
                      check_failure<test_exception>(failedFuture, "failure");
                      return move_only(42);
                  };
        }

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<move_only>,
    "future_recover_move_only_type_recover_failure_before_recover_initialized_with_custom_scheduler_on_rvalue") {
    {
        auto error = false;

        sut = async(make_executor<0>(), [&_error = error]() -> move_only {
                  _error = true;
                  throw test_exception("failure");
              }).recover(make_executor<1>(), [](auto failedFuture) {
            check_failure<test_exception>(failedFuture, "failure");
            return move_only(42);
        });

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;

        sut = async(make_executor<0>(),
                    [&_error = error]() -> move_only {
                        _error = true;
                        throw test_exception("failure");
                    }) ^
              (executor{make_executor<1>()} & [](auto failedFuture) {
                  check_failure<test_exception>(failedFuture, "failure");
                  return move_only(42);
              });

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(
    test_fixture<move_only>,
    "future_recover_move_only_types_recover_failure_after_recover_initialized_with_custom_scheduler_on_rvalue") {
    {
        auto error = false;
        mutex block;
        {
            lock_t const hold(block);
            sut = async(make_executor<0>(), [&_error = error, &_block = block]() -> move_only {
                      lock_t const lock(_block);
                      _error = true;
                      throw test_exception("failure");
                  }).recover(make_executor<1>(), [](auto failedFuture) {
                check_failure<test_exception>(failedFuture, "failure");
                return move_only(42);
            });
        }

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }

    custom_scheduler<0>::reset();
    custom_scheduler<1>::reset();

    {
        auto error = false;
        mutex block;
        {
            lock_t const hold(block);
            sut = async(make_executor<0>(),
                        [&_error = error, &_block = block]() -> move_only {
                            lock_t const lock(_block);
                            _error = true;
                            throw test_exception("failure");
                        }) ^
                  (executor{make_executor<1>()} & [](auto failedFuture) {
                      check_failure<test_exception>(failedFuture, "failure");
                      return move_only(42);
                  });
        }

        auto result = await(std::move(sut));

        REQUIRE(result.member() == 42);
        REQUIRE(error);
        REQUIRE(get_c0() == 1);
        REQUIRE(get_c1() >= 1);
    }
}

TEST_CASE_FIXTURE(test_fixture<move_only>, "future_recover_move_only_with_broken_promise") {
    //"running future move-only recover with broken promise");

    {
        auto check{false};
        sut = [&check]() {
            auto p{
                package<move_only(move_only)>(immediate_executor, [](move_only x) { return x; })};
            return std::move(p.second).recover([&check](const auto& f) {
                check = true;
                try {
                    return *std::move(f.get_try());
                } catch (const exception&) {
                    throw;
                }
            });
        }();

        check_failure<future_error>(sut, "broken promise");
        REQUIRE(check);
    }
    {
        auto check{false};
        sut = [&check]() {
            auto p{
                package<move_only(move_only)>(immediate_executor, [](move_only x) { return x; })};
            return std::move(p.second) ^ [&check](const auto& f) {
                check = true;
                try {
                    return *std::move(f.get_try());
                } catch (const exception&) {
                    throw;
                }
            };
        }();

        check_failure<future_error>(sut, "broken promise");
        REQUIRE(check);
    }
}
