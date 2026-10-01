/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#ifndef STLAB_SRC_CONCURRENCY_DETAIL_TIMER_COMMON_HPP
#define STLAB_SRC_CONCURRENCY_DETAIL_TIMER_COMMON_HPP

#include <stlab/concurrency/system_timer.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <system_error>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// Checks the relocation ABI and the normalized timer delay before any resource preparation.
inline void check_timer_submission(const unsigned char* guard, std::int64_t delay_ns) noexcept {
    if (guard != &current_task_storage_abi_guard::value || delay_ns < 0) {
        assert(false && "system timer task storage ABI mismatch or negative ABI delay");
        std::terminate();
    }
}

/// Diagnoses submission after shutdown even in builds with assertions disabled.
inline void check_timer_open(bool closed) noexcept {
    if (closed) {
        assert(false && "system timer submission after pre_exit()");
        std::terminate();
    }
}

/// Translates only the two error categories supported by the timer ABI.
inline auto timer_resource_error(const std::system_error& error) noexcept -> stlab_v2_timer_status {
    if (error.code().category() == std::generic_category())
        return {2, static_cast<std::int32_t>(error.code().value())};
    if (error.code().category() == std::system_category())
        return {3, static_cast<std::int32_t>(error.code().value())};
    assert(false && "unsupported timer resource error category");
    std::terminate();
}

/// Monotonic remaining delay, rechecked before execution; long waits use bounded clock additions.
class timer_delay {
    using clock = std::chrono::steady_clock;
    clock::time_point _sample = clock::now();
    std::int64_t _remaining;

public:
    /// Records the accepted nonnegative nanosecond delay.
    explicit timer_delay(std::int64_t delay_ns) noexcept : _remaining(delay_ns) {}

    /// Updates and returns the remaining delay, rounding elapsed fractional nanoseconds down.
    auto remaining() noexcept -> std::int64_t {
        const auto now = clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - _sample);
        assert(elapsed.count() >= 0);
        _remaining = elapsed.count() >= _remaining ? 0 : _remaining - elapsed.count();
        _sample = now;
        return _remaining;
    }

    /// Returns a bounded next wake, avoiding overflow for a delay near INT64_MAX.
    auto next_wake() noexcept -> clock::time_point {
        constexpr std::int64_t chunk =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::hours(1)).count();
        const auto wait = std::chrono::ceil<clock::duration>(
            std::chrono::nanoseconds(std::min(remaining(), chunk)));
        if (_sample > clock::time_point::max() - wait) return clock::time_point::max();
        return _sample + wait;
    }
};

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

#endif
