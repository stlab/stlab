/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_CONCURRENCY_SYSTEM_TIMER_HPP
#define STLAB_CONCURRENCY_SYSTEM_TIMER_HPP

/*! @file system_timer.hpp
 *  @brief Asynchronous delayed execution through the process-shared timer service.
 *
 *  Positive delays round upward to the timer resolution; execution can be late, never early.
 *  Nonpositive delays and past steady-clock deadlines request asynchronous execution without delay.
 *  `pre_exit()` closes admission, destroys canceled captures, and waits for committed callbacks on
 *  other threads through one core cleanup handler registered at first timer or default-executor
 * use. Application handlers registered afterward run first, in LIFO order, and can signal
 * callbacks. The core handler releases pending timer captures before joining default executors.
 * When `pre_exit()` blocks the main thread, workers and timer callbacks must not synchronously
 * require main-queue progress. The shared core handler does not close or drain the main queue.
 * On native platforms `pre_exit()` must not be called from a timer callback. Client modules
 * supplying task operations must remain loaded until their accepted tasks have completed or have
 * been canceled and destroyed.
 */

#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>
#include <stlab/pre_exit.hpp> // IWYU pragma: export

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <system_error>
#include <type_traits>

namespace stlab {
inline namespace v2 {

/// Resource result: code 0 is success, 1 allocation failure, 2 generic-category error,
/// and 3 system-category error. `native_error` is the category's numeric error value.
struct stlab_v2_timer_status {
    std::int32_t code;
    std::int32_t native_error;
};

static_assert(std::is_standard_layout_v<stlab_v2_timer_status>);
static_assert(sizeof(stlab_v2_timer_status) == 2 * sizeof(std::int32_t));
static_assert(offsetof(stlab_v2_timer_status, native_error) == sizeof(std::int32_t));

/// Invocation operation for a relocated `void() noexcept` task.
using stlab_v2_task_proc = void (*)(void*) noexcept;

/// Accepts a task into the core timer service without propagating exceptions.
///
/// - Precondition: the task ABI guard and relocation operations describe the live task at `source`;
///   `delay_ns` is nonnegative, and `pre_exit()` has not closed timer admission.
/// - Postcondition: success relocates the target exactly once; failure leaves `source` unconsumed.
///   Accepted targets are invoked once or canceled, and destroyed once.
extern "C" stlab_v2_timer_status stlab_v2_system_timer_submit(const unsigned char* task_abi_guard,
                                                              const stlab_v2_task_concept* vtable,
                                                              stlab_v2_task_proc invoke,
                                                              void* source,
                                                              std::int64_t delay_ns) noexcept;

} // namespace v2

STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_system_timer system_timer
 *  @ingroup stlab_concurrency
 *  @brief Asynchronous delayed execution through the process-shared timer service.
 *  @{
 */

namespace detail {

/// Returns ceil(`count * numerator * multiplier / denominator`) without overflowing products.
///
/// - Precondition: ratio factors fit `intmax_t`; the denominator and multipliers are positive.
inline auto timer_integral_nanoseconds(std::uintmax_t count,
                                       std::uintmax_t numerator,
                                       std::uintmax_t denominator,
                                       std::uintmax_t multiplier = 1) -> std::int64_t {
    constexpr auto limit = static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max());
    std::uintmax_t quotient = 0;
    std::uintmax_t remainder = 0;
    if (count <= std::numeric_limits<std::uintmax_t>::max() / numerator) {
        const auto product = count * numerator;
        quotient = product / denominator;
        remainder = product % denominator;
    } else {
        // Long division avoids overflowing the intermediate product for fractional periods.
        const auto whole = numerator / denominator;
        const auto fraction = numerator % denominator;
        for (int bit = std::numeric_limits<std::uintmax_t>::digits - 1; bit >= 0; --bit) {
            if (quotient > limit / 2)
                throw std::overflow_error("system_timer delay exceeds signed nanoseconds");
            quotient *= 2;
            remainder *= 2; // The ratio denominator is at most INTMAX_MAX.
            auto carry = remainder / denominator;
            remainder %= denominator;
            if ((count >> bit) & 1) {
                if (whole > limit - carry)
                    throw std::overflow_error("system_timer delay exceeds signed nanoseconds");
                carry += whole;
                remainder += fraction;
                carry += remainder / denominator;
                remainder %= denominator;
            }
            if (carry > limit - quotient)
                throw std::overflow_error("system_timer delay exceeds signed nanoseconds");
            quotient += carry;
        }
    }
    if (quotient > limit / multiplier)
        throw std::overflow_error("system_timer delay exceeds signed nanoseconds");
    quotient *= multiplier;
    const auto rounded_fraction =
        multiplier == 1 ? static_cast<std::uintmax_t>(remainder != 0) :
                          static_cast<std::uintmax_t>(
                              timer_integral_nanoseconds(remainder, multiplier, denominator));
    if (rounded_fraction > limit - quotient)
        throw std::overflow_error("system_timer delay exceeds signed nanoseconds");
    return static_cast<std::int64_t>(quotient + rounded_fraction);
}

/// Normalizes nonpositive delays and rounds positive finite delays upward to signed nanoseconds.
template <typename Rep, typename Period>
auto timer_nanoseconds(std::chrono::duration<Rep, Period> duration) -> std::int64_t {
    if constexpr (std::is_floating_point_v<Rep>) {
        if (!std::isfinite(duration.count()))
            throw std::invalid_argument("system_timer delay must be finite");
    }
    if (duration.count() <= 0) return 0;
    if constexpr (std::is_integral_v<Rep>) {
        return timer_integral_nanoseconds(static_cast<std::uintmax_t>(duration.count()),
                                          Period::num, Period::den, 1000000000);
    } else {
        const auto value =
            std::ceil(static_cast<long double>(duration.count()) *
                      static_cast<long double>(Period::num) / Period::den * 1000000000);
        // 2^63 is exactly representable even when long double has only double precision.
        const auto bound = -static_cast<long double>(std::numeric_limits<std::int64_t>::min());
        if (!std::isfinite(value) || value >= bound)
            throw std::overflow_error("system_timer delay exceeds signed nanoseconds");
        return value < 1 ? 1 : static_cast<std::int64_t>(value);
    }
}

/// Submits a normalized delay and translates only the ABI's explicit resource failures.
inline void submit_system_timer(std::int64_t delay_ns, task<void() noexcept>& f) {
    const auto result =
        stlab_v2_system_timer_submit(&current_task_storage_abi_guard::value, f.relocation_concept(),
                                     f.relocation_invoke(), f.relocation_source(), delay_ns);
    switch (result.code) {
        case 0:
            return;
        case 1:
            throw std::bad_alloc();
        case 2:
            throw std::system_error(result.native_error, std::generic_category());
        case 3:
            throw std::system_error(result.native_error, std::system_category());
        default:
            assert(false && "invalid timer ABI resource status");
            std::terminate();
    }
}

/// Adapter to the core-owned timer service.
struct system_timer_type {
    using result_type = void;

    /// Schedules `f` asynchronously no earlier than `when`, or without delay for past deadlines.
    ///
    /// - Precondition: timer admission has not been closed by `pre_exit()`.
    /// - Throws: `std::overflow_error` if the remaining delay exceeds signed nanoseconds;
    ///   `std::bad_alloc` or `std::system_error` on submission resource failure.
    /// - Complexity: backend-dependent; portable submission is amortized logarithmic in the
    ///   number of pending timers, with a linear worst case when queue storage grows.
    void operator()(std::chrono::steady_clock::time_point when, task<void() noexcept>&& f) const {
        const auto now = std::chrono::steady_clock::now();
        if (when <= now) {
            submit_system_timer(0, f);
            return;
        }
        using duration = std::chrono::steady_clock::duration;
        using unsigned_rep = std::make_unsigned_t<typename duration::rep>;
        const auto remaining = static_cast<unsigned_rep>(when.time_since_epoch().count()) -
                               static_cast<unsigned_rep>(now.time_since_epoch().count());
        submit_system_timer(
            timer_nanoseconds(
                std::chrono::duration<unsigned_rep, typename duration::period>(remaining)),
            f);
    }

    /// Schedules `f` asynchronously after `duration`, rounded upward; nonpositive delays do not
    /// wait.
    ///
    /// - Precondition: timer admission has not been closed by `pre_exit()`.
    /// - Throws: `std::invalid_argument` for nonfinite input, `std::overflow_error` for positive
    ///   delays exceeding signed nanoseconds, or `std::bad_alloc`/`std::system_error` for resource
    ///   failure. Failure leaves the supplied task unconsumed.
    /// - Complexity: backend-dependent; portable submission is amortized logarithmic in the
    ///   number of pending timers, with a linear worst case when queue storage grows.
    template <typename Rep, typename Period>
    void operator()(std::chrono::duration<Rep, Period> duration, task<void() noexcept>&& f) const {
        submit_system_timer(timer_nanoseconds(duration), f);
    }
};

} // namespace detail

/// Schedules move-only `void() noexcept` tasks through the process-shared timer service.
inline constexpr auto system_timer = detail::system_timer_type{};

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

#endif
