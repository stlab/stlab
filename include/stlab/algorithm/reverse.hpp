/*
    Copyright 2013 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

#ifndef STLAB_ALGORITHM_REVERSE_HPP
#define STLAB_ALGORITHM_REVERSE_HPP

/*! @file reverse.hpp
 *  @brief Reverse algorithms for iterators and intrusive forward / bidirectional sequences.
 */

#include <algorithm>
#include <utility>

#include <stlab/config.hpp>
#include <stlab/iterator/concepts.hpp>
#include <stlab/iterator/set_next.hpp>

/**************************************************************************************************/

/** @defgroup stlab_algorithm_reverse reverse
 *  @ingroup stlab_algorithm
 *  @brief Reverse algorithms for iterators and intrusive forward sequences.
 *  @{
 */

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

namespace unsafe {

/**************************************************************************************************/

/// Reverses the range `[first, last)` and appends `end`. Returns the beginning of the
/// reversed range such that `[result, end)` is a valid range.

template <STLAB_CONCEPT(FORWARD_NODE_ITERATOR) I>
auto reverse_append(I first, I last, I end) -> I {
    while (first != last) {
        I prior(end);
        ++first;
        stlab::unsafe::set_next(prior, end);
        end = prior;
    }
    return end;
}

/// Reverses `range` and appends `end`.  Returns the beginning of the reversed range such that
/// `[result, end)` is a valid range.

template <typename R, // R models NodeRange
          typename I> // I models NodeIterator
inline auto reverse_append(R& range, I end) -> I {
    return stlab::unsafe::reverse_append(std::begin(range), std::end(range), end);
}

/// Reverses the range `[first, last)` and returns the beginning of the reversed range such that
/// `[result, last)` is a valid range.

template <typename I> // I models NodeIterator
inline auto reverse_nodes(I first, I last) -> I {
    return stlab::unsafe::reverse_append(first, last, last);
}

/// Reverses the range `range` and returns the beginning of the reversed range such that
/// `[result, last)` is a valid range.

template <typename R> // R models NodeRange
inline auto reverse_nodes(R& range) -> typename R::iterator {
    return stlab::unsafe::reverse_nodes(std::begin(range), std::end(range));
}

/**************************************************************************************************/

} // namespace unsafe

/**************************************************************************************************/

/// Range-based `reverse`, will be deprecated in C++20 in favor of
/// [`std::ranges::reverse`](https://en.cppreference.com/w/cpp/algorithm/ranges/reverse).

template <class BidirectionalRange>
inline void reverse(BidirectionalRange& range) {
    std::reverse(std::begin(range), std::end(range));
}

/// Range-based `reverse_copy` algorithm, will be deprecated in C++20 in favor of
/// [`std::ranges::reverse_copy`](https://en.cppreference.com/w/cpp/algorithm/ranges/reverse_copy).
template <class BidirectionalRange, class OutputIterator>
inline void reverse_copy(BidirectionalRange& range, OutputIterator result) {
    std::reverse_copy(std::begin(range), std::end(range), result);
}

/// Range-based `reverse_copy` algorithm, will be deprecated in C++20 in favor of
/// [`std::ranges::reverse_copy`](https://en.cppreference.com/w/cpp/algorithm/ranges/reverse_copy).
template <class BidirectionalRange, class OutputIterator>
inline void reverse_copy(const BidirectionalRange& range, OutputIterator result) {
    std::reverse_copy(std::begin(range), std::end(range), result);
}

/**************************************************************************************************/

/// Reverses the range `[f, l)` until `m` is reached. Returns a range [a, b) of the un-reversed
/// subrange such that either `a == m` or `b == m`.

template <typename I> // I models BidirectionalIterator
auto reverse_until(I f, I m, I l) -> std::pair<I, I> {
    while (f != m && m != l) {
        --l;

        std::iter_swap(f, l);

        ++f;
    }

    return std::pair<I, I>(f, l);
}

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

/** @} */

#endif // STLAB_ALGORITHM_REVERSE_HPP

/**************************************************************************************************/
