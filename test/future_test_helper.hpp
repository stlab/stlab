/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef FUTURE_TEST_HELPER_HPP_
#define FUTURE_TEST_HELPER_HPP_

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>

#include <atomic>
#include <condition_variable>
#include <stdexcept>
#include <string>
#include <thread>

#include <doctest/doctest.h>

using lock_t = std::unique_lock<std::mutex>;

namespace future_test_helper {

template <typename E, typename F>
void check_failure(F& f, const char* message) {
#if 1
    try {
        (void)f.get_try();
        FAIL("Expected exception was not thrown");

    } catch (const std::exception& e) {
        REQUIRE(typeid(E) == typeid(e));
        REQUIRE(std::string(message) == std::string(e.what()));
    }
#else
    // This version fails in CI with the XCode 16.4 runner. It appears to be a compiler bug.
    REQUIRE_THROWS_AS((void)f.get_try(), E);
#endif
}

struct when_any_result_helper {
    template <typename T>
    auto operator()(T&& value, size_t index) const {
        return std::pair{std::forward<T>(value), index};
    }
    auto operator()(size_t index) const { return index; }
};

constexpr auto when_any_result = when_any_result_helper{};

template <std::size_t no>
struct custom_scheduler {
    using result_type = void;

    void operator()(stlab::task<void() noexcept> f) const {
        ++counter();
        stlab::default_executor(std::move(f));
    }

    static auto usage_counter() -> int { return counter().load(); }

    static void reset() { counter() = 0; }

    static auto counter() -> std::atomic_int& {
        static std::atomic_int counter;
        return counter;
    }

private:
    size_t _id = no; // only used for debugging purpose
};

template <std::size_t I>
auto make_executor() -> stlab::executor_t {
    return [_executor = custom_scheduler<I>{}](stlab::task<void() noexcept> f) mutable {
        _executor(std::move(f));
    };
}

class test_exception : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;

    test_exception(const char* message) : std::runtime_error(message) {}
    ~test_exception() = default;
    test_exception(const test_exception&) = default;
    auto operator=(const test_exception&) -> test_exception& = default;
    test_exception(test_exception&&) noexcept = default;
    auto operator=(test_exception&&) noexcept -> test_exception& = default;
};

struct test_setup {
    test_setup() {
        custom_scheduler<0>::reset();
        custom_scheduler<1>::reset();
    }
};

template <typename T>
struct test_fixture {
    test_fixture() : _task_counter{0} {
        custom_scheduler<0>::reset();
        custom_scheduler<1>::reset();
    }

    ~test_fixture() = default;

    stlab::future<T> sut;

    template <typename... F>
    void wait_until_future_completed(F&&... f) {
        (void)std::initializer_list<int>{(wait_until_future_is_ready(std::forward<F>(f)), 0)...};
    }

    void check_valid_future() {}

    void check_valid_future(const stlab::future<T>& f) { REQUIRE(f.valid() == true); }

    template <typename F, typename... FS>
    void check_valid_future(const F& f, const FS&... fs) {
        REQUIRE(f.valid() == true);
        check_valid_future(fs...);
    }

    template <typename E, typename... F>
    void wait_until_future_fails(F&&... f) {
        (void)std::initializer_list<int>{
            (wait_until_this_future_fails<E>(std::forward<F>(f)), 0)...};
    }

    void wait_until_all_tasks_completed() {
        while (_task_counter.load() != 0) {
            stlab::invoke_waiting(
                [] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
        }
    }

    std::atomic_int _task_counter{0};

private:
    template <typename F>
    void wait_until_future_is_ready(F&& f) {
        (void)stlab::await(std::forward<F>(f));
    }

    template <typename E, typename F>
    void wait_until_this_future_fails(F&& f) {
        try {
            (void)stlab::await(std::forward<F>(f));
        } catch (const E&) {
        } catch (...) {
            FAIL("Unexpected exception thrown in wait_until_this_future_fails");
        }
    }
};

struct thread_block_context {
    std::shared_ptr<std::mutex> _mutex;
    std::condition_variable _thread_block;
    std::atomic_bool _go{false};
    std::atomic_bool _may_proceed{false};

    thread_block_context() : _mutex(std::make_shared<std::mutex>()) {}
};

template <typename F, typename P>
class test_functor_base : public P {
    F _f;
    std::atomic_int* _task_counter{nullptr};

public:
    test_functor_base(F f, std::atomic_int& task_counter) :
        _f(std::move(f)), _task_counter(&task_counter) {
        ++*_task_counter;
    }

    ~test_functor_base() {
        if (_task_counter) --*_task_counter;
    }

    test_functor_base(const test_functor_base&) = delete;
    auto operator=(const test_functor_base&) -> test_functor_base& = delete;

    test_functor_base(test_functor_base&& a) noexcept :
        P{std::move(a)}, _f{std::move(a._f)},
        _task_counter{std::exchange(a._task_counter, nullptr)} {}

    auto operator=(test_functor_base&& a) noexcept -> test_functor_base& {
        P::operator=(std::move(a)); // move base
        _f = std::move(a._f);
        _task_counter = std::exchange(a._task_counter, nullptr);
        return *this;
    }

    template <typename... Args>
    auto operator()(Args&&... args) const {
        assert(_task_counter);
        P::action();
        return _f(std::forward<Args>(args)...);
    }
};

struct null_policy {
    void action() const {}
};

class blocking_policy {
    thread_block_context* _context{nullptr};

public:
    void set_context(thread_block_context* context) { _context = context; }

    void action() const {
        lock_t lock(*_context->_mutex);
        stlab::invoke_waiting([&] {
            _context->_thread_block.wait(lock,
                                         [&] { return _context->_go && _context->_may_proceed; });
        });
    }
};

template <typename F>
auto make_non_blocking_functor(F&& f, std::atomic_int& task_counter) {
    return test_functor_base<F, null_policy>(std::forward<F>(f), task_counter);
}

template <typename F>
auto make_blocking_functor(F&& f, std::atomic_int& task_counter, thread_block_context& context) {
    auto result = test_functor_base<F, blocking_policy>(std::forward<F>(f), task_counter);
    result.set_context(&context);
    return result;
}

} // namespace future_test_helper

#endif
