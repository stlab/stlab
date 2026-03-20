/*
    Copyright 2013 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
/**************************************************************************************************/

#ifndef STLAB_FOREST_HPP
#define STLAB_FOREST_HPP

/*! @file forest.hpp
 *  @brief Forest (tree-of-sequences) iterators, ranges, and algorithms.
 */

/**************************************************************************************************/

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>

#include <stlab/algorithm/reverse.hpp>
#include <stlab/config.hpp>
#include <stlab/iterator/set_next.hpp>

/**************************************************************************************************/

/** @defgroup stlab_forest forest
 *  @ingroup stlab
 *  @brief Forest (tree-of-sequences) iterators, ranges, and algorithms.
 *  @{
 */

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

enum class forest_edge : bool { trailing, leading };

constexpr auto forest_trailing_edge{forest_edge::trailing};
constexpr auto forest_leading_edge{forest_edge::leading};

/**************************************************************************************************/

constexpr auto pivot(forest_edge e) { return forest_edge(static_cast<bool>(e) ^ 1); }

template <class I> // I models FullorderIterator
void pivot(I& i) {
    i.edge() = pivot(i.edge());
}

template <class I> // I models FullorderIterator
auto pivot_of(I i) {
    pivot(i);
    return i;
}

/**************************************************************************************************/

template <class I> // I models a FullorderIterator
auto leading_of(I i) {
    i.edge() = forest_edge::leading;
    return i;
}

template <class I> // I models a FullorderIterator
auto trailing_of(I i) {
    i.edge() = forest_edge::trailing;
    return i;
}

/**************************************************************************************************/

constexpr auto is_leading(forest_edge e) { return e == forest_edge::leading; }

template <class I> // I models a FullorderIterator
auto is_leading(const I& i) {
    return is_leading(i.edge());
}

constexpr auto is_trailing(forest_edge e) { return e == forest_edge::trailing; }

template <class I> // I models a FullorderIterator
auto is_trailing(const I& i) {
    return is_trailing(i.edge());
}

/**************************************************************************************************/

/// Returns the trailing-edge iterator of the parent of the subtree containing `i`.
///
/// @note Time complexity is linear in the number of nodes visited along the search.
template <class I> // I models FullorderIterator
auto find_parent(I i) -> I {
    do {
        i = std::next(trailing_of(i));
    } while (i.edge() != forest_edge::trailing);
    return i;
}

/**************************************************************************************************/

template <class I> // I models FullorderIterator
auto has_children(const I& i) -> bool {
    return !i.equal_node(std::next(leading_of(i)));
}

/**************************************************************************************************/

template <class I> // I models FullorderIterator
struct child_iterator {
    using value_type = typename std::iterator_traits<I>::value_type;
    using difference_type = typename std::iterator_traits<I>::difference_type;
    using reference = typename std::iterator_traits<I>::reference;
    using pointer = typename std::iterator_traits<I>::pointer;
    using iterator_category = typename std::iterator_traits<I>::iterator_category;

    child_iterator() = default;
    explicit child_iterator(const I& x) : _x(std::move(x)) {}
    template <class U>
    child_iterator(const child_iterator<U>& u) : _x(u.base()) {}

    [[nodiscard]] auto base() const -> I { return _x; }

    auto operator*() -> reference { return dereference(); }
    auto operator->() -> pointer { return &dereference(); }
    auto operator++() -> auto& {
        increment();
        return *this;
    }
    auto operator++(int) -> child_iterator {
        auto result{*this};
        increment();
        return result;
    }
    auto operator--() -> auto& {
        decrement();
        return *this;
    }
    auto operator--(int) -> child_iterator {
        auto result{*this};
        decrement();
        return result;
    }

    friend auto operator==(const child_iterator& a, const child_iterator& b) -> bool {
        return a.base() == b.base();
    }
    friend auto operator!=(const child_iterator& a, const child_iterator& b) -> bool {
        return !(a == b);
    }

private:
    I _x;

    void increment() {
        pivot(_x);
        ++_x;
    }
    void decrement() {
        --_x;
        pivot(_x);
    }

    auto dereference() -> reference { return *_x; }
};

/**************************************************************************************************/

template <class I> // I models FullorderIterator
auto find_edge(I x, forest_edge edge) -> I {
    while (x.edge() != edge)
        ++x;
    return x;
}

template <class I> // I models FullorderIterator
auto find_edge_reverse(I x, forest_edge edge) -> I {
    while (x.edge() != edge)
        --x;
    return x;
}

/**************************************************************************************************/

template <class I, forest_edge Edge> // I models FullorderIterator
struct edge_iterator {
    using value_type = typename std::iterator_traits<I>::value_type;
    using difference_type = typename std::iterator_traits<I>::difference_type;
    using reference = typename std::iterator_traits<I>::reference;
    using pointer = typename std::iterator_traits<I>::pointer;
    using iterator_category = typename std::iterator_traits<I>::iterator_category;

    edge_iterator() = default;
    explicit edge_iterator(const I& x) : _x(find_edge(x, Edge)) {}
    template <class U>
    edge_iterator(const edge_iterator<U, Edge>& u) : _x(u.base()) {}

    [[nodiscard]] auto base() const -> I { return _x; }

    auto operator*() -> reference { return dereference(); }
    auto operator->() -> pointer { return &dereference(); }
    auto operator++() -> auto& {
        increment();
        return *this;
    }
    auto operator++(int) -> edge_iterator {
        auto result{*this};
        increment();
        return result;
    }
    auto operator--() -> auto& {
        decrement();
        return *this;
    }
    auto operator--(int) -> edge_iterator {
        auto result{*this};
        decrement();
        return result;
    }

    friend auto operator==(const edge_iterator& a, const edge_iterator& b) -> bool {
        return a.base() == b.base();
    }
    friend auto operator!=(const edge_iterator& a, const edge_iterator& b) -> bool {
        return !(a == b);
    }

private:
    I _x;

    void increment() { _x = find_edge(std::next(_x), Edge); }

    void decrement() { _x = find_edge_reverse(std::prev(_x), Edge); }

    auto dereference() -> reference { return *_x; }
};

/**************************************************************************************************/

template <class I, // I models a Forest
          class P> // P models UnaryPredicate of value_type(I)
struct filter_fullorder_iterator {
    using value_type = typename std::iterator_traits<I>::value_type;
    using difference_type = typename std::iterator_traits<I>::difference_type;
    using reference = typename std::iterator_traits<I>::reference;
    using pointer = typename std::iterator_traits<I>::pointer;
    using iterator_category = typename std::iterator_traits<I>::iterator_category;

    filter_fullorder_iterator() = default;

    filter_fullorder_iterator(I f, I l, P p) :
        _x(skip_forward(f, l, p)), _inside(true), _first(f), _last(l), _predicate(p) {}

    filter_fullorder_iterator(I f, I l) :
        _x(skip_forward(f, l, P())), _inside(true), _first(f), _last(l) {}

    template <class U>
    filter_fullorder_iterator(const filter_fullorder_iterator<U, P>& x) :
        _x(x.base()), _inside(x._inside), _first(x._first), _last(x._last),
        _predicate(x._predicate) {}

    auto predicate() const -> P { return _predicate; }

    [[nodiscard]] auto edge() const -> forest_edge { return this->base().edge(); }
    auto edge() -> forest_edge& { return this->base_reference().edge(); }

    auto equal_node(const filter_fullorder_iterator& y) const -> bool {
        return this->base().equal_node(y.base());
    }

    auto base() const -> I { return _x; }
    auto base_reference() -> I& { return _x; }

    auto operator*() -> reference { return dereference(); }
    auto operator->() -> pointer { return &dereference(); }
    auto operator++() -> auto& {
        increment();
        return *this;
    }
    auto operator++(int) -> filter_fullorder_iterator {
        auto result{*this};
        increment();
        return result;
    }
    auto operator--() -> auto& {
        decrement();
        return *this;
    }
    auto operator--(int) -> filter_fullorder_iterator {
        auto result{*this};
        decrement();
        return result;
    }

    friend auto operator==(const filter_fullorder_iterator& a, const filter_fullorder_iterator& b)
        -> bool {
        return a.base() == b.base();
    }
    friend auto operator!=(const filter_fullorder_iterator& a, const filter_fullorder_iterator& b)
        -> bool {
        return !(a == b);
    }

private:
    I _x;
    bool _inside;
    I _first;
    I _last;
    P _predicate;

    void increment() {
        I i = this->base();

        if (i == _last) _inside = false;
        ++i;
        if (i == _first) _inside = true;
        if (_inside) i = skip_forward(i, _last, _predicate);
        this->base_reference() = i;
    }

    static auto skip_forward(I f, I l, P p) -> I
    // Precondition: l is on a leading edge
    {
        while ((f.edge() == forest_edge::leading) && (f != l) && !p(*f)) {
            f.edge() = forest_edge::trailing;
            ++f;
        }
        return f;
    }

    static auto skip_backward(I f, I l, P p) -> I
    // Precondition: f is on a trailing edge
    {
        while ((l.edge() == forest_edge::trailing) && (f != l) && !p(*l)) {
            l.edge() = forest_edge::leading;
            --l;
        }
        return l;
    }

    void decrement() {
        I i = this->base();

        if (i == _first) _inside = false;
        --i;
        if (i == _last) _inside = true;
        if (_inside) i = skip_backward(_first, i, _predicate);
        this->base_reference() = i;
    }

    auto dereference() -> reference { return *_x; }
};

/**************************************************************************************************/

template <class I> // I models a FullorderIterator
struct reverse_fullorder_iterator {
    using iterator_type = I;

    using value_type = typename std::iterator_traits<I>::value_type;
    using difference_type = typename std::iterator_traits<I>::difference_type;
    using reference = typename std::iterator_traits<I>::reference;
    using pointer = typename std::iterator_traits<I>::pointer;
    using iterator_category = typename std::iterator_traits<I>::iterator_category;

    reverse_fullorder_iterator() : _edge(forest_edge::trailing) {}
    explicit reverse_fullorder_iterator(I x) : _base(--x), _edge(pivot(_base.edge())) {}
    template <class U>
    reverse_fullorder_iterator(const reverse_fullorder_iterator<U>& x) :
        _base(--x.base()), _edge(pivot(_base.edge())) {}

    [[nodiscard]] auto base() const -> iterator_type { return std::next(_base); }

    [[nodiscard]] auto edge() const -> forest_edge { return _edge; }
    auto edge() -> forest_edge& { return _edge; }

    [[nodiscard]] auto equal_node(const reverse_fullorder_iterator& y) const -> bool {
        return _base.equal_node(y._base);
    }

    auto operator*() -> reference { return dereference(); }
    auto operator->() -> pointer { return &dereference(); }
    auto operator++() -> auto& {
        increment();
        return *this;
    }
    auto operator++(int) -> reverse_fullorder_iterator {
        auto result{*this};
        increment();
        return result;
    }
    auto operator--() -> auto& {
        decrement();
        return *this;
    }
    auto operator--(int) -> reverse_fullorder_iterator {
        auto result{*this};
        decrement();
        return result;
    }

    friend auto operator==(const reverse_fullorder_iterator& a, const reverse_fullorder_iterator& b)
        -> bool {
        return a.equal(b);
    }
    friend auto operator!=(const reverse_fullorder_iterator& a, const reverse_fullorder_iterator& b)
        -> bool {
        return !(a == b);
    }

private:
    I _base;
    forest_edge _edge;

    void increment() {
        _base.edge() = pivot(_edge);
        --_base;
        _edge = pivot(_base.edge());
    }
    void decrement() {
        _base.edge() = pivot(_edge);
        ++_base;
        _edge = pivot(_base.edge());
    }
    [[nodiscard]] auto dereference() const -> reference { return *_base; }

    [[nodiscard]] auto equal(const reverse_fullorder_iterator& y) const -> bool {
        return (_base == y._base) && (_edge == y._edge);
    }
};

/**************************************************************************************************/

template <class I> // I models FullorderIterator
struct depth_fullorder_iterator {
    using value_type = typename std::iterator_traits<I>::value_type;
    using difference_type = typename std::iterator_traits<I>::difference_type;
    using reference = typename std::iterator_traits<I>::reference;
    using pointer = typename std::iterator_traits<I>::pointer;
    using iterator_category = typename std::iterator_traits<I>::iterator_category;

    depth_fullorder_iterator(difference_type d = 0) : _depth(d) {}
    explicit depth_fullorder_iterator(I x, difference_type d = 0) : _x(x), _depth(d) {}
    template <class U>
    depth_fullorder_iterator(const depth_fullorder_iterator<U>& x) :
        _x(x.base()), _depth(x._depth) {}

    auto depth() const -> difference_type { return _depth; }
    [[nodiscard]] auto edge() const -> forest_edge { return this->base().edge(); }
    auto edge() -> forest_edge& { return _x.edge(); }
    auto equal_node(depth_fullorder_iterator const& y) const -> bool {
        return this->base().equal_node(y.base());
    }

    auto base() const -> I { return _x; }

    auto operator*() -> reference { return dereference(); }
    auto operator->() -> pointer { return &dereference(); }
    auto operator++() -> auto& {
        increment();
        return *this;
    }
    auto operator++(int) -> depth_fullorder_iterator {
        auto result{*this};
        increment();
        return result;
    }
    auto operator--() -> auto& {
        decrement();
        return *this;
    }
    auto operator--(int) -> depth_fullorder_iterator {
        auto result{*this};
        decrement();
        return result;
    }

    friend auto operator==(const depth_fullorder_iterator& a, const depth_fullorder_iterator& b)
        -> bool {
        return a.base() == b.base();
    }
    friend auto operator!=(const depth_fullorder_iterator& a, const depth_fullorder_iterator& b)
        -> bool {
        return !(a == b);
    }

private:
    I _x;
    difference_type _depth;

    void increment() {
        forest_edge old_edge(edge());
        ++_x;
        if (old_edge == edge())
            _depth += difference_type(static_cast<std::size_t>(old_edge) << 1) - 1;
    }

    void decrement() {
        forest_edge old_edge(edge());
        --_x;
        if (old_edge == edge())
            _depth -= difference_type(static_cast<std::size_t>(old_edge) << 1) - 1;
    }

    auto dereference() -> reference { return *_x; }
};

/**************************************************************************************************/

template <class Forest>
class child_adaptor;
template <class T>
class forest;

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <class D> // derived class
struct node_base {
    enum next_prior_t : std::uint8_t { prior_s, next_s };

    using node_ptr = D*;
    using reference = node_ptr&;

    auto link(forest_edge edge, next_prior_t link) -> node_ptr& {
        return _nodes[static_cast<std::size_t>(edge)][static_cast<std::size_t>(link)];
    }

    [[nodiscard]] auto link(forest_edge edge, next_prior_t link) const -> node_ptr {
        return _nodes[static_cast<std::size_t>(edge)][static_cast<std::size_t>(link)];
    }

    std::array<std::array<node_ptr, 2>, 2> _nodes{nullptr};

    node_base() :
        _nodes{std::array{static_cast<node_ptr>(this), static_cast<node_ptr>(this)},
               std::array{static_cast<node_ptr>(this), static_cast<node_ptr>(this)}} {}
};

template <class T> // T models Regular
struct node : public node_base<node<T>> {
    using value_type = T;

    explicit node(value_type data) : _data(std::move(data)) {}

    value_type _data;
};

/**************************************************************************************************/

template <class T>
struct forest_const_iterator;

template <class T> // T is value_type
struct forest_iterator {
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using pointer = T*;
    using iterator_category = std::bidirectional_iterator_tag;

    forest_iterator() = default;

    forest_iterator(const forest_iterator& x) : _node(x._node), _edge(x._edge) {}

    auto operator=(const forest_iterator&) -> forest_iterator& = default;

    [[nodiscard]] auto edge() const -> forest_edge { return _edge; }
    auto edge() -> forest_edge& { return _edge; }

    [[nodiscard]] auto equal_node(forest_iterator const& y) const -> bool {
        return _node == y._node;
    }

    auto operator*() const -> reference { return dereference(); }
    auto operator->() const -> pointer { return &dereference(); }
    auto operator++() -> auto& {
        increment();
        return *this;
    }
    auto operator++(int) -> forest_iterator {
        auto result{*this};
        increment();
        return result;
    }
    auto operator--() -> auto& {
        decrement();
        return *this;
    }
    auto operator--(int) -> forest_iterator {
        auto result{*this};
        decrement();
        return result;
    }

    friend auto operator==(const forest_iterator& a, const forest_iterator& b) -> bool {
        return a.equal(b);
    }
    friend auto operator!=(const forest_iterator& a, const forest_iterator& b) -> bool {
        return !(a == b);
    }

private:
    friend class stlab::forest<value_type>;
    template <class>
    friend struct forest_iterator;
    template <class>
    friend struct forest_const_iterator;
    friend struct unsafe::set_next_fn<forest_iterator>;

    using node_t = node<T>;

    [[nodiscard]] auto dereference() const -> reference { return _node->_data; }

    void increment() {
        node_t* next(_node->link(_edge, node_t::next_s));

        if (is_leading(_edge))
            _edge = forest_edge(next != _node);
        else
            _edge = forest_edge(next->link(forest_edge::leading, node_t::prior_s) == _node);

        _node = next;
    }

    void decrement() {
        node_t* next(_node->link(_edge, node_t::prior_s));

        if (is_leading(_edge))
            _edge = forest_edge(next->link(forest_edge::trailing, node_t::next_s) != _node);
        else
            _edge = forest_edge(next == _node);

        _node = next;
    }

    [[nodiscard]] auto equal(const forest_iterator& y) const -> bool {
        return (_node == y._node) && (_edge == y._edge);
    }

    node_t* _node{nullptr};
    forest_edge _edge{forest_edge::leading};

    forest_iterator(node_t* node, forest_edge edge) : _node(node), _edge(edge) {}
};

/**************************************************************************************************/

template <class T> // T is value_type
struct forest_const_iterator {
    using value_type = const T;
    using difference_type = std::ptrdiff_t;
    using reference = const T&;
    using pointer = const T*;
    using iterator_category = std::bidirectional_iterator_tag;

    forest_const_iterator() = default;

    forest_const_iterator(const forest_const_iterator& x) : _node(x._node), _edge(x._edge) {}

    auto operator=(const forest_const_iterator&) -> forest_const_iterator& = default;

    forest_const_iterator(const forest_iterator<T>& x) : _node(x._node), _edge(x._edge) {}

    [[nodiscard]] auto edge() const -> forest_edge { return _edge; }
    auto edge() -> forest_edge& { return _edge; }
    [[nodiscard]] auto equal_node(forest_const_iterator const& y) const -> bool {
        return _node == y._node;
    }

    auto operator*() const -> reference { return dereference(); }
    auto operator->() const -> pointer { return &dereference(); }
    auto operator++() -> auto& {
        increment();
        return *this;
    }
    auto operator++(int) -> forest_const_iterator {
        auto result{*this};
        increment();
        return result;
    }
    auto operator--() -> auto& {
        decrement();
        return *this;
    }
    auto operator--(int) -> forest_const_iterator {
        auto result{*this};
        decrement();
        return result;
    }

    friend auto operator==(const forest_const_iterator& a, const forest_const_iterator& b) -> bool {
        return a.equal(b);
    }
    friend auto operator!=(const forest_const_iterator& a, const forest_const_iterator& b) -> bool {
        return !(a == b);
    }

private:
    template <class>
    friend class stlab::forest;
    template <class>
    friend struct forest_const_iterator;
    friend struct unsafe::set_next_fn<forest_const_iterator>;

    using node_t = const node<T>;

    [[nodiscard]] auto dereference() const -> reference { return _node->_data; }

    void increment() {
        node_t* next(_node->link(_edge, node_t::next_s));

        if (is_leading(_edge))
            _edge = forest_edge(next != _node);
        else
            _edge = forest_edge(next->link(forest_edge::leading, node_t::prior_s) == _node);

        _node = next;
    }

    void decrement() {
        node_t* next(_node->link(_edge, node_t::prior_s));

        if (is_leading(_edge))
            _edge = forest_edge(next->link(forest_edge::trailing, node_t::next_s) != _node);
        else
            _edge = forest_edge(next == _node);

        _node = next;
    }

    [[nodiscard]] auto equal(const forest_const_iterator& y) const -> bool {
        return (_node == y._node) && (_edge == y._edge);
    }

    node_t* _node{nullptr};
    forest_edge _edge{forest_edge::leading};

    forest_const_iterator(node_t* node, forest_edge edge) : _node(node), _edge(edge) {}
};

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

namespace unsafe {

/**************************************************************************************************/

template <class T> // T is node<T>
struct set_next_fn<detail::forest_iterator<T>> {
    void operator()(detail::forest_iterator<T> x, detail::forest_iterator<T> y) const {
        using node_t = typename detail::node<T>;

        x._node->link(x.edge(), node_t::next_s) = y._node;
        y._node->link(y.edge(), node_t::prior_s) = x._node;
    }
};

/**************************************************************************************************/

} // namespace unsafe

/**************************************************************************************************/

template <class T>
class forest {
private:
    using node_t = detail::node<T>;
    friend class child_adaptor<forest<T>>;

public:
    // types
    using reference = T&;
    using const_reference = const T&;
    using iterator = detail::forest_iterator<T>;
    using const_iterator = detail::forest_const_iterator<T>;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reverse_iterator = reverse_fullorder_iterator<iterator>;
    using const_reverse_iterator = reverse_fullorder_iterator<const_iterator>;

    /* qualification needed since: A name N used in a class S shall refer to the same declaration
       in its context and when re-evaluated in the completed scope of
       S. */
    using child_iterator = stlab::child_iterator<iterator>;
    using const_child_iterator = stlab::child_iterator<const_iterator>;
    using reverse_child_iterator = std::reverse_iterator<child_iterator>;

    using preorder_iterator = edge_iterator<iterator, forest_edge::leading>;
    using const_preorder_iterator = edge_iterator<const_iterator, forest_edge::leading>;
    using postorder_iterator = edge_iterator<iterator, forest_edge::trailing>;
    using const_postorder_iterator = edge_iterator<const_iterator, forest_edge::trailing>;

    forest() = default;
    ~forest() { clear(); }

    forest(const forest& x) {
        insert(end(), const_child_iterator(x.begin()), const_child_iterator(x.end()));
    }
    forest(forest&& x) noexcept : forest() { splice(end(), x); }
    auto operator=(const forest& x) -> forest& {
        // self-assignment is not allowed to disable cert-oop54-cpp warning (and is likely a bug)
        assert(this != &x && "self-assignment is not allowed");
        return *this = forest(x);
    }

    auto operator=(forest&& x) noexcept -> forest& {
        auto tmp{std::move(x)}; // this is `release()`
        clear();                // these two lines are `reset()`
        splice(end(), tmp);
        return *this;
    }

    void swap(forest& x) noexcept { std::swap(*this, x); }

    /// Returns the node count; constant time when `size_valid()` is true, otherwise a full walk.
    ///
    /// @note Some splices can invalidate cached sizes on source and destination forests.
    auto size() const -> size_type;
    auto max_size() const -> size_type { return size_type(-1); }
    auto size_valid() const -> bool { return _size != 0 || empty(); }
    auto empty() const -> bool {
        return begin() == end();
    } // Don't test size which may be expensive

    // iterators
    auto root() -> iterator { return iterator(tail(), forest_edge::leading); }
    auto root() const -> const_iterator { return const_iterator(tail(), forest_edge::leading); }

    auto begin() -> iterator { return ++root(); }
    auto end() -> iterator { return iterator(tail(), forest_edge::trailing); }
    auto begin() const -> const_iterator { return ++root(); }
    auto end() const -> const_iterator { return const_iterator(tail(), forest_edge::trailing); }

    auto rbegin() -> reverse_iterator { return reverse_iterator(end()); }
    auto rend() -> reverse_iterator { return reverse_iterator(begin()); }
    auto rbegin() const -> const_reverse_iterator { return const_reverse_iterator(end()); }
    auto rend() const -> const_reverse_iterator { return const_reverse_iterator(begin()); }

    auto front() -> reference {
        assert(!empty());
        return *begin();
    }
    auto front() const -> const_reference {
        assert(!empty());
        return *begin();
    }
    auto back() -> reference {
        assert(!empty());
        return *(--end());
    }
    auto back() const -> const_reference {
        assert(!empty());
        return *(--end());
    }

    // modifiers
    void clear() {
        erase(begin(), end());
        assert(empty()); // Make sure our erase is correct
    }

    /// Erases the node at `position` (children become siblings of the former parent’s subtree) or
    /// the half-open range `[first, last)`.
    /// @{
    auto erase(const iterator& position) -> iterator;
    auto erase(const iterator& first, const iterator& last) -> iterator;
    /// @}

    auto insert(const iterator& position, T x) -> iterator {
        iterator result(new node_t(std::move(x)), forest_edge::leading);

        if (size_valid()) ++_size;

        unsafe::set_next(std::prev(position), result);
        unsafe::set_next(std::next(result), position);

        return result;
    }

    void push_front(const T& x) { insert(begin(), x); }
    void push_back(const T& x) { insert(end(), x); }
    void pop_front() {
        assert(!empty());
        erase(begin());
    }
    void pop_back() {
        assert(!empty());
        erase(--end());
    }

    auto insert(iterator position,
                const const_child_iterator& first,
                const const_child_iterator& last) -> iterator;

    auto splice(const iterator& position, forest<T>& x) -> iterator;
    auto splice(const iterator& position, forest<T>& x, iterator i) -> iterator;
    auto splice(const iterator& position, forest<T>& x, child_iterator first, child_iterator last)
        -> iterator;
    auto splice(const iterator& position,
                forest<T>& x,
                const child_iterator& first,
                const child_iterator& last,
                size_type count) -> iterator;

    auto insert_parent(child_iterator front, child_iterator back, const T& x) -> iterator;
    void reverse(child_iterator first, child_iterator last);

private:
    friend struct detail::forest_iterator<value_type>;
    friend struct detail::forest_const_iterator<value_type>;
    friend struct unsafe::set_next_fn<iterator>;

    mutable std::atomic<size_type> _size{0};
    detail::node_base<node_t> _tail;

    auto tail() -> node_t* { return static_cast<node_t*>(&_tail); }
    auto tail() const -> const node_t* { return static_cast<const node_t*>(&_tail); }
};

/**************************************************************************************************/

template <class T>
auto operator==(const forest<T>& x, const forest<T>& y) -> bool {
    if (x.size() != y.size()) return false;

    for (auto first(x.begin()), last(x.end()), pos(y.begin()); first != last; ++first, ++pos) {
        if (first.edge() != pos.edge()) return false;
        if (is_leading(first) && (*first != *pos)) return false;
    }

    return true;
}

template <class T>
auto operator!=(const forest<T>& x, const forest<T>& y) -> bool {
    return !(x == y);
}

/**************************************************************************************************/

namespace unsafe {

/**************************************************************************************************/

template <class I> // I models a FullorderIterator
struct set_next_fn<child_iterator<I>> {
    void operator()(const child_iterator<I>& x, const child_iterator<I>& y) {
        unsafe::set_next(pivot_of(x.base()), y.base());
    }
};

/**************************************************************************************************/

} // namespace unsafe

/**************************************************************************************************/

template <class T>
auto forest<T>::size() const -> size_type {
    if (!size_valid()) {
        const_preorder_iterator first(begin());
        const_preorder_iterator last(end());

        _size = size_type(std::distance(first, last));
    }

    return _size;
}

/**************************************************************************************************/

template <class T>
auto forest<T>::erase(const iterator& first, const iterator& last) -> iterator {
    difference_type stack_depth(0);
    iterator position(first);

    while (position != last) {
        if (position.edge() == forest_edge::leading) {
            ++stack_depth;
            ++position;
        } else {
            if (stack_depth > 0)
                position = erase(position);
            else
                ++position;
            stack_depth = std::max<difference_type>(0, stack_depth - 1);
        }
    }
    return last;
}

/**************************************************************************************************/

template <class T>
auto forest<T>::erase(const iterator& position) -> iterator {
    /*
        NOTE (sparent) : After the first call to set_next() the invariants of the forest are
        violated and we can't determine leading/trailing if we navigate from the affected node.
        So we gather all the iterators up front then do the set_next calls.
    */

    if (size_valid()) --_size;

    iterator leading_prior(std::prev(leading_of(position)));
    iterator leading_next(std::next(leading_of(position)));
    iterator trailing_prior(std::prev(trailing_of(position)));
    iterator trailing_next(std::next(trailing_of(position)));

    if (has_children(position)) {
        unsafe::set_next(leading_prior, leading_next);
        unsafe::set_next(trailing_prior, trailing_next);
    } else {
        unsafe::set_next(leading_prior, trailing_next);
    }

    delete position._node;

    return is_leading(position) ? std::next(leading_prior) : trailing_next;
}

/**************************************************************************************************/

template <class T>
auto forest<T>::splice(const iterator& position, forest<T>& x) -> iterator {
    return splice(position, x, child_iterator(x.begin()), child_iterator(x.end()),
                  x.size_valid() ? x.size() : 0);
}

/**************************************************************************************************/

template <class T>
auto forest<T>::splice(const iterator& position, forest<T>& x, iterator i) -> iterator {
    i.edge() = forest_edge::leading;
    return splice(position, x, child_iterator(i), ++child_iterator(i), has_children(i) ? 0 : 1);
}

/**************************************************************************************************/

template <class T>
auto forest<T>::insert(iterator pos, const const_child_iterator& f, const const_child_iterator& l)
    -> iterator {
    for (const_iterator first(f.base()), last(l.base()); first != last; ++first, ++pos) {
        if (is_leading(first)) pos = insert(pos, *first);
    }

    return pos;
}

/**************************************************************************************************/

template <class T>
auto forest<T>::splice(const iterator& position,
                       forest<T>& x,
                       const child_iterator& first,
                       const child_iterator& last,
                       size_type count) -> iterator {
    if (first == last || first.base() == position) return position;

    if (&x != this) {
        if (count) {
            if (size_valid()) _size += count;
            if (x.size_valid()) x._size -= count;
        } else {
            _size = 0;
            x._size = 0;
        }
    }

    iterator back(std::prev(last.base()));

    unsafe::set_next(std::prev(first), last);

    unsafe::set_next(std::prev(position), first.base());
    unsafe::set_next(back, position);

    return first.base();
}

/**************************************************************************************************/

template <class T>
auto forest<T>::splice(const iterator& position,
                       forest<T>& x,
                       child_iterator first,
                       child_iterator last) -> iterator {
    return splice(position, x, first, last, 0);
}

/**************************************************************************************************/

template <class T>
auto forest<T>::insert_parent(child_iterator first, child_iterator last, const T& x) ->
    typename forest<T>::iterator {
    iterator result(insert(last.base(), x));
    if (first == last) return result;
    splice(trailing_of(result), *this, first, child_iterator(result));
    return result;
}

/**************************************************************************************************/

template <class T>
void forest<T>::reverse(child_iterator first, child_iterator last) {
    iterator prior(first.base());
    --prior;
    first = unsafe::reverse_nodes(first, last);
    unsafe::set_next(prior, first.base());
}

/**************************************************************************************************/

template <class I> // I models FullorderIterator
auto child_begin(const I& x) -> child_iterator<I> {
    return child_iterator<I>(std::next(leading_of(x)));
}

/**************************************************************************************************/

template <class I> // I models FullorderIterator
auto child_end(const I& x) -> child_iterator<I> {
    return child_iterator<I>(trailing_of(x));
}

/**************************************************************************************************/

template <class Forest>
class child_adaptor {
public:
    using forest_type = Forest;
    using value_type = typename Forest::value_type;
    using iterator_type = typename Forest::iterator;
    using reference = typename Forest::reference;
    using const_reference = typename Forest::const_reference;
    using difference_type = typename Forest::difference_type;
    using iterator = typename Forest::child_iterator;

    child_adaptor() = delete; // not defined
    child_adaptor(forest_type& f, iterator_type& i) : _forest(f), _iterator(i) {}

    auto back() -> value_type& { return *(--child_end(_iterator)); }
    auto front() -> value_type& { return *(child_begin(_iterator)); }

    void push_back(const value_type& x) { _forest.insert(child_end(_iterator).base(), x); }
    void push_front(const value_type& x) { _forest.insert(child_begin(_iterator).base(), x); }

    void pop_back() { _forest.erase(--child_end(_iterator).base()); }
    void pop_front() { _forest.erase(child_begin(_iterator).base()); }

private:
    forest_type& _forest;
    iterator_type& _iterator;
};

/**************************************************************************************************/

template <class I>
struct forest_range {
    I _f;
    I _l;

    [[nodiscard]] auto begin() const { return _f; }
    [[nodiscard]] auto end() const { return _l; }
};

/**************************************************************************************************/

template <class I> // I models FullorderIterator
auto child_range(const I& x) {
    return forest_range<child_iterator<I>>{child_begin(x), child_end(x)};
}

/**************************************************************************************************/

template <class R, typename P> // R models FullorderRange
auto filter_fullorder_range(R& x, P p) {
    using iterator = filter_fullorder_iterator<typename R::iterator, P>;

    return forest_range<iterator>{iterator(std::begin(x), std::end(x), p),
                                  iterator(std::end(x), std::end(x), p)};
}

template <class R, typename P> // R models FullorderRange
auto filter_fullorder_range(const R& x, P p) {
    using iterator = filter_fullorder_iterator<typename R::const_iterator, P>;

    return forest_range<iterator>{iterator(std::begin(x), std::end(x), p),
                                  iterator(std::end(x), std::end(x), p)};
}

/**************************************************************************************************/

template <class R> // R models FullorderRange
auto reverse_fullorder_range(R& x) {
    using iterator = reverse_fullorder_iterator<typename R::iterator>;

    return forest_range<iterator>{iterator(std::end(x)), iterator(std::begin(x))};
}

template <class R> // R models FullorderRange
auto reverse_fullorder_range(const R& x) {
    using iterator = reverse_fullorder_iterator<typename R::const_iterator>;

    return forest_range<iterator>{iterator(std::end(x)), iterator(std::begin(x))};
}

/**************************************************************************************************/

template <class R> // R models FullorderRange
auto depth_range(R& x) {
    using iterator = depth_fullorder_iterator<typename R::iterator>;

    return forest_range<iterator>{iterator(std::begin(x)), iterator(std::end(x))};
}

template <class R> // R models FullorderRange
auto depth_range(const R& x) {
    using iterator = depth_fullorder_iterator<typename R::const_iterator>;

    return forest_range<iterator>{iterator(std::begin(x)), iterator(std::end(x))};
}

/**************************************************************************************************/

template <class R> // R models FullorderRange
auto postorder_range(R& x) {
    using iterator = edge_iterator<typename R::iterator, forest_edge::trailing>;

    return forest_range<iterator>{iterator(std::begin(x)), iterator(std::end(x))};
}

template <class R> // R models FullorderRange
auto postorder_range(const R& x) {
    using iterator = edge_iterator<typename R::const_iterator, forest_edge::trailing>;

    return forest_range<iterator>{iterator(std::begin(x)), iterator(std::end(x))};
}

/**************************************************************************************************/

template <class R> // R models FullorderRange
auto preorder_range(R& x) {
    using iterator = edge_iterator<typename R::iterator, forest_edge::leading>;

    return forest_range<iterator>{iterator(std::begin(x)), iterator(std::end(x))};
}

template <class R> // R models FullorderRange
auto preorder_range(const R& x) {
    using iterator = edge_iterator<typename R::const_iterator, forest_edge::leading>;

    return forest_range<iterator>{iterator(std::begin(x)), iterator(std::end(x))};
}

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

/** @} */

#endif // STLAB_FOREST_HPP

/**************************************************************************************************/
