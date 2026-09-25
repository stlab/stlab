/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#pragma once

#include <stlab/config.hpp>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// Tracks whether a portable executor worker is waiting or has received a wake.
class waiter_state {
    bool _waiting{false};
    bool _wake_pending{false};
    bool _done{false};

public:
    /// Starts waiting unless a wake or shutdown is already pending.
    ///
    /// - Postcondition: returns `true` only when the caller should block.
    auto begin_wait() noexcept -> bool {
        if (_done) return false;
        if (_wake_pending) {
            _wake_pending = false;
            return false;
        }
        _waiting = true;
        return true;
    }

    /// Records a wake request, signaling the worker only when it is waiting.
    ///
    /// - Postcondition: returns `true` only when a waiting worker was signaled.
    auto wake() noexcept -> bool {
        if (_done) return false;
        if (_waiting) {
            _waiting = false;
            return true;
        }
        _wake_pending = true;
        return false;
    }

    /// Records that the worker should terminate.
    void done() noexcept { _done = true; }

    /// Reports whether the worker is currently waiting.
    auto waiting() const noexcept -> bool { return _waiting; }

    /// Reports whether shutdown was requested.
    auto is_done() const noexcept -> bool { return _done; }
};

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab
