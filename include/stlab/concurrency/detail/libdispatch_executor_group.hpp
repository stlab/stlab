/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_DETAIL_LIBDISPATCH_EXECUTOR_GROUP_HPP
#define STLAB_CONCURRENCY_DETAIL_LIBDISPATCH_EXECUTOR_GROUP_HPP

#include <stlab/config.hpp>

#include <dispatch/dispatch.h>

#include <utility>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {

/// Owns the libdispatch group used to coordinate default-executor work.
struct group_t {
    dispatch_group_t _group = dispatch_group_create();

    /// Constructs a dispatch group owner.
    group_t() = default;

    /// Disables copying of the dispatch group owner.
    group_t(const group_t&) = delete;

    /// Moves a dispatch group owner.
    group_t(group_t&& a) noexcept : _group(std::exchange(a._group, nullptr)) {}

    /// Disables assignment of the dispatch group owner.
    auto operator=(const group_t&) -> group_t& = delete;

    /// Move-assigns a dispatch group owner.
    auto operator=(group_t&& a) noexcept -> group_t& {
        _group = std::exchange(a._group, nullptr);
        return *this;
    }

    /// Waits for outstanding work and releases the dispatch group.
    ~group_t();
};

/// Returns the libdispatch group that tracks shared executor work.
auto group() -> const group_t&;

} // namespace detail
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
