/*
    Copyright 2013 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

#ifndef STLAB_ITERATOR_SET_NEXT_HPP
#define STLAB_ITERATOR_SET_NEXT_HPP

#include <iterator>

#include <stlab/config.hpp>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace unsafe {

/**************************************************************************************************/

template <typename I> // I models NodeIterator
struct set_next_fn;   // Must be specialized

/**************************************************************************************************/

template <typename I> // I models NodeIterator
inline void set_next(const I& x, const I& y) {
    set_next_fn<I>()(x, y);
}

/**************************************************************************************************/

template <typename I> // T models ForwardNodeIterator
inline void splice_node_range(I location, I first, I last) {
    I successor(std::next(location));
    set_next(location, first);
    set_next(last, successor);
}

template <typename I> // I models ForwardNodeIterator
inline void skip_next_node(I location) {
    set_next(location, std::next(std::next(location)));
}

template <typename I> // I models BidirectionalNodeIterator
inline void skip_node(I location) {
    set_next(std::prev(location), std::next(location));
}

/**************************************************************************************************/

} // namespace unsafe
STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

/**************************************************************************************************/

#endif // STLAB_ITERATOR_SET_NEXT_HPP

/**************************************************************************************************/
