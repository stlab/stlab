/**************************************************************************************************/

#ifndef STLAB_FOREST_ALGORITHMS_HPP
#define STLAB_FOREST_ALGORITHMS_HPP

/*! @file forest_algorithms.hpp
 *  @brief Higher-level forest algorithms (`stlab::forests`).
 */

/**************************************************************************************************/

// stdc++
#include <optional>

// stlab
#include <stlab/forest.hpp>

/**************************************************************************************************/

/** @defgroup stlab_forest_algorithms forest_algorithms
 *  @ingroup stlab
 *  @brief Higher-level forest algorithms (`stlab::forests`).
 *  @{
 */

namespace stlab::forests {

/**************************************************************************************************/
// "Congruent" would be a nice name here, but in geometry that also implies reflection.
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

template <class Container>
auto transcriber(Container& c) {
    return transcribe_iterator<Container>(c, c.begin());
}

/**************************************************************************************************/

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

template <class R, class O, class P, class UP>
auto transcribe(const R& range, O out, P proj, UP pred) {
    return transcribe(std::cbegin(range), std::cend(range), std::move(out), std::move(proj),
                      std::move(pred));
}

template <class I, class O, class P>
auto transcribe(const I& first, const I& last, O out, P proj) {
    return transcribe(first, last, std::move(out), std::move(proj),
                      [](const auto& p) { return is_leading(p); });
}

template <class R, class O, class P>
auto transcribe(const R& range, O out, P proj) {
    return transcribe(std::cbegin(range), std::cend(range), std::move(out), std::move(proj));
}

/**************************************************************************************************/

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

template <class I, // I models ForwardIterator; I::value_type == std::optional<T>
          class F> // F models Forest
auto unflatten(I first, I last, F& f) {
    return forests::transcribe(
        first, last, transcriber(f), [](const auto& x) { return *x; },
        [](const auto& p) { return *p; });
}

/**************************************************************************************************/

} // namespace stlab::forests

/**************************************************************************************************/

/** @} */

#endif // STLAB_FOREST_ALGORITHMS_HPP

/**************************************************************************************************/
