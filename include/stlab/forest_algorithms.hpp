/**************************************************************************************************/

#ifndef STLAB_FOREST_ALGORITHMS_HPP
#define STLAB_FOREST_ALGORITHMS_HPP

/*! @file forest_algorithms.hpp
 *  @brief Forest utilities in namespace `stlab::forests` (shape checks, flatten/transcribe, etc.).
 *
 *  @details
 *  Algorithms that operate on @ref stlab_forest "forest" full-order iterators: compare tree shape
 *  without comparing values (`equal_shape`), copy structure with a projection (`transcribe`),
 *  serialize leading nodes to a flat sequence with placeholders for trailing edges (`flatten`),
 *  and rebuild a forest from that representation (`unflatten`).
 */

/**************************************************************************************************/

// stdc++
#include <optional>

// stlab
#include <stlab/forest.hpp>

/**************************************************************************************************/

namespace stlab::forests {

/** @defgroup stlab_forest_algorithms forest_algorithms
 *  @ingroup stlab
 *  @brief Higher-level forest algorithms (`stlab::forests`).
 *
 *  @details
 *  Declarations live in this header; see the `@file` description for an overview of
 *  `equal_shape`, `transcribe`, `flatten`, and `unflatten`.
 *  @{
 */

/**************************************************************************************************/

/// Returns `true` if `x` and `y` have the same full-order edge sequence (values may differ).
///
/// @details
/// ("Congruent" would be a nice name here, but in geometry that also implies reflection.)
/// If both forests have a valid cached `size()` and they differ, returns `false` immediately.
template <class Forest1, class Forest2>
auto equal_shape(const Forest1& x, const Forest2& y) -> bool {
    if (x.size_valid() && y.size_valid() && x.size() != y.size()) return false;
    auto pos{y.begin()};
    for (auto first(x.begin()), last(x.end()); first != last; ++first, ++pos) {
        if (first.edge() != pos.edge()) return false;
    }
    return true;
}

/**************************************************************************************************/

/// Output iterator that inserts projected values into a forest (or similar container).
///
/// @details
/// Assignment inserts at the current position; `trailing()` advances past a trailing edge without
/// inserting a node value.
template <class Container>
struct transcribe_iterator {
    using iterator_category = std::output_iterator_tag;
    using value_type = void;
    using difference_type = void;
    using pointer = void;
    using reference = void;
    using container_type = Container;

    transcribe_iterator(Container& c, const typename Container::iterator& i) : _c{&c}, _i{i} {}

    constexpr auto operator*() -> auto& { return *this; }
    constexpr auto operator++() -> auto& {
        ++_i;
        return *this;
    }
    constexpr auto operator++(int) -> auto {
        auto result{*this};
        ++_i;
        return result;
    }

    constexpr auto operator=(const typename Container::value_type& value) -> auto& {
        _i = _c->insert(_i, value);
        return *this;
    }

    constexpr auto operator=(typename Container::value_type&& value) -> auto& {
        _i = _c->insert(_i, std::move(value));
        return *this;
    }

    constexpr auto trailing() { _i = trailing_of(_i); }

private:
    Container* _c;
    typename Container::iterator _i;
};

/// Returns a `transcribe_iterator` starting at `c.begin()` for building `c` from a walk.
template <class Container>
auto transcriber(Container& c) {
    return transcribe_iterator<Container>(c, c.begin());
}

/**************************************************************************************************/

/// Walks `[first, last)`; for positions where `pred` is true, assigns `proj(*first)` to `out`,
/// otherwise calls `out.trailing()`.
template <class I, class O, class P, class UP>
auto transcribe(I first, const I& last, O out, P proj, UP pred) {
    for (; first != last; ++first, ++out) {
        if (pred(first)) {
            out = proj(*first);
        } else {
            out.trailing();
        }
    }
    return out;
}

/// Range overload of `transcribe` with explicit leading/trailing predicate `pred`.
template <class R, class O, class P, class UP>
auto transcribe(const R& range, O out, P proj, UP pred) {
    return transcribe(std::cbegin(range), std::cend(range), std::move(out), std::move(proj),
                      std::move(pred));
}

/// Walks `[first, last)` using `is_leading` as the predicate (default transcribe behavior).
template <class I, class O, class P>
auto transcribe(const I& first, const I& last, O out, P proj) {
    return transcribe(first, last, std::move(out), std::move(proj),
                      [](const auto& p) { return is_leading(p); });
}

/// Range overload of `transcribe` with `is_leading` as the predicate.
template <class R, class O, class P>
auto transcribe(const R& range, O out, P proj) {
    return transcribe(std::cbegin(range), std::cend(range), std::move(out), std::move(proj));
}

/**************************************************************************************************/

/// Writes each leading node's value to `out`; writes `std::nullopt` at trailing edges.
///
/// @details
/// The result is a lossless encoding of forest shape plus leading values (see `unflatten`).
template <class I, // models ForestFullorderIterator
          class O> // models OutputIterator
auto flatten(I first, const I& last, O out) {
    for (; first != last; ++first) {
        if (is_leading(first)) {
            *out++ = *first;
        } else {
            *out++ = std::nullopt;
        }
    }
    return out;
}

/**************************************************************************************************/

/// Rebuilds forest `f` from a flattened `std::optional` sequence (inverse of `flatten`).
template <class I, // I models ForwardIterator; I::value_type == std::optional<T>
          class F> // F models Forest
auto unflatten(I first, I last, F& f) {
    return forests::transcribe(
        first, last, transcriber(f), [](const auto& x) { return *x; },
        [](const auto& p) { return *p; });
}

/**************************************************************************************************/

/** @} */

} // namespace stlab::forests

/**************************************************************************************************/

#endif // STLAB_FOREST_ALGORITHMS_HPP

/**************************************************************************************************/
