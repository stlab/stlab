/**************************************************************************************************/

// stdc++
#include <cassert>
#include <cstddef>
#include <iterator>
#include <optional>

// boost
#include <doctest/doctest.h>

// stlab
#include <stlab/forest.hpp>
#include <stlab/forest_algorithms.hpp>
#include <stlab/utility.hpp>
#include <string>
#include <utility>
#include <vector>

/**************************************************************************************************/

using namespace stlab;

/**************************************************************************************************/

TEST_CASE("empty_forest") {
    forest<int> f;
    REQUIRE(f.empty());
    REQUIRE(f.size() == 0);
    REQUIRE(f.begin() == f.end());
}

/**************************************************************************************************/

TEST_CASE("single_node_forest") {
    forest<int> f;

    auto il = f.insert(f.end(), 42);
    REQUIRE(il.edge() == forest_edge::leading);
    REQUIRE(*il == 42);
    REQUIRE(!f.empty());

    REQUIRE(f.begin() != f.end());
    REQUIRE(f.size() == 1);

    auto it = trailing_of(il);
    REQUIRE(it.edge() == forest_edge::trailing);
    REQUIRE(*it == *il);
}

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

auto big_test_forest() {
    forest<std::string> f;

    auto a_iter = trailing_of(f.insert(f.end(), "A"));

    auto b_iter = trailing_of(f.insert(a_iter, "B"));

    auto c_iter = trailing_of(f.insert(b_iter, "C"));
    auto d_iter = trailing_of(f.insert(b_iter, "D"));
    auto e_iter = f.insert(b_iter, "E");

    f.insert(c_iter, "F");
    f.insert(c_iter, "G");
    f.insert(c_iter, "H");

    f.insert(d_iter, "I");
    f.insert(d_iter, "J");
    f.insert(d_iter, "K");

    REQUIRE(f.size() == 11);

    REQUIRE(has_children(a_iter));
    REQUIRE(has_children(b_iter));
    REQUIRE(has_children(c_iter));
    REQUIRE(has_children(d_iter));
    REQUIRE(!has_children(e_iter));

    return f;
}

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

template <typename T>
auto to_string(const T& x) {
    return std::to_string(x);
}

template <>
auto to_string(const std::string& x) {
    return x;
}

template <>
auto to_string(const std::optional<std::string>& x) {
    return x ? to_string(*x) : "?";
}

/**************************************************************************************************/

} // namespace detail

/**************************************************************************************************/

template <typename R>
auto to_string(const R& r) {
    std::string result;
    for (const auto& x : r) {
        result += detail::to_string(x);
    }
    return result;
}

template <typename I>
auto to_string(I first, const I& last) {
    std::string result;
    while (first != last) {
        result += *first++;
    }
    return result;
}

/**************************************************************************************************/

template <typename Iterator>
void test_fullorder_traversal(const Iterator& first,
                              const Iterator& last,
                              const std::string& expected) {
    REQUIRE(to_string(first, last) == expected);
}

/**************************************************************************************************/

template <typename Iterator, forest_edge Edge, typename Forest>
auto test_edge_traversal(Forest& f, const Iterator& fi, const Iterator& li) {
    std::string expected;

    {
        Iterator first{fi};
        const Iterator& last{li};
        while (first != last) {
            if (first.edge() == Edge) expected += *first;
            ++first;
        }
        REQUIRE(expected.size() == f.size());
    }

    {
        edge_iterator<Iterator, Edge> first(fi);
        edge_iterator<Iterator, Edge> last(li);
        std::string result{to_string(first, last)};
        REQUIRE(result == expected);
    }

    return expected;
}

/**************************************************************************************************/

using iterator_t = forest<std::string>::iterator;
using const_iterator_t = forest<std::string>::const_iterator;
using reverse_iterator_t = forest<std::string>::reverse_iterator;
using const_reverse_iterator_t = forest<std::string>::const_reverse_iterator;

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

TEST_CASE("forward_traversal") {
    auto f{big_test_forest()};
    auto first{std::begin(f)};
    auto last{std::end(f)};

    /*fullorder*/ {
        static const auto expected{"ABCFFGGHHCDIIJJKKDEEBA"};
        test_fullorder_traversal<iterator_t>(first, last, expected);
        test_fullorder_traversal<const_iterator_t>(first, last, expected);

#if 0 // fullorder_range doesn't exist?
        REQUIRE(range_value(fullorder_range(f)) == expected);
#endif
    }

    /*preorder*/ {
        auto a = test_edge_traversal<iterator_t, forest_edge::leading>(f, first, last);
        auto b = test_edge_traversal<const_iterator_t, forest_edge::leading>(f, first, last);
        REQUIRE(a == b);
        REQUIRE(to_string(preorder_range(f)) == a);
    }

    /*postorder*/ {
        auto a = test_edge_traversal<iterator_t, forest_edge::trailing>(f, first, last);
        auto b = test_edge_traversal<const_iterator_t, forest_edge::trailing>(f, first, last);
        REQUIRE(a == b);
        REQUIRE(to_string(postorder_range(f)) == a);
    }
}

/**************************************************************************************************/

TEST_CASE("reverse_traversal") {
    auto f{big_test_forest()};
    auto rfirst{std::rbegin(f)};
    auto rlast{std::rend(f)};

    /*fullorder*/ {
        static const auto expected{"ABEEDKKJJIIDCHHGGFFCBA"};
        test_fullorder_traversal<reverse_iterator_t>(rfirst, rlast, expected);
        test_fullorder_traversal<const_reverse_iterator_t>(rfirst, rlast, expected);
        REQUIRE(to_string(reverse_fullorder_range(f)) == expected);
    }

    /*preorder*/ {
        auto a = test_edge_traversal<reverse_iterator_t, forest_edge::leading>(f, rfirst, rlast);
        auto b =
            test_edge_traversal<const_reverse_iterator_t, forest_edge::leading>(f, rfirst, rlast);
        REQUIRE(a == b);
    }

    /*postorder*/ {
        auto a = test_edge_traversal<reverse_iterator_t, forest_edge::trailing>(f, rfirst, rlast);
        auto b =
            test_edge_traversal<const_reverse_iterator_t, forest_edge::trailing>(f, rfirst, rlast);
        REQUIRE(a == b);
    }
}

/**************************************************************************************************/

template <typename Forest, typename T>
auto find_node(Forest& f, const T& x) {
    return std::find_if(f.begin(), f.end(), [&](const auto& v) { return v == x; });
}

/**************************************************************************************************/

TEST_CASE("child_traversal") {
    auto f{big_test_forest()};
    auto parent{find_node(f, "B")};
    std::string expected;

    REQUIRE(*parent == "B");

    {
        auto first{child_begin(parent)};
        auto last{child_end(parent)};
        expected = to_string(first, last);
    }

    REQUIRE(to_string(child_range(parent)) == expected);

#if 0
        // I'm not sure reverse_child_iterator ever worked.
        forest<std::string>::reverse_child_iterator first{child_begin(parent)};
        forest<std::string>::reverse_child_iterator last{child_end(parent)};
        std::string result{to_string(first, last)};
        REQUIRE(result == expected);
#endif
}

/**************************************************************************************************/

TEST_CASE("test_find_parent") {
    auto f{big_test_forest()};

    {
        auto child{find_node(f, "B")};
        REQUIRE(*child == "B");
        auto parent{find_parent(child)};
        REQUIRE(*parent == "A");
    }

    {
        auto child{find_node(f, "J")};
        REQUIRE(*child == "J");
        auto parent{find_parent(child)};
        REQUIRE(*parent == "D");
    }
}

/**************************************************************************************************/

TEST_CASE("test_has_children") {
    auto f{big_test_forest()};

    /*pass*/ {
        auto node{find_node(f, "B")};
        REQUIRE(*node == "B");
        REQUIRE(has_children(node));
    }

    /*fail*/ {
        auto node{find_node(f, "J")};
        REQUIRE(*node == "J");
        REQUIRE(!has_children(node));
    }
}

/**************************************************************************************************/

TEST_CASE("erase") {
    auto f{big_test_forest()};

    /*single node*/ {
        auto node{find_node(f, "J")};
        REQUIRE(*node == "J");
        auto erased_result = f.erase(node);
        std::string result{to_string(preorder_range(f))};
        REQUIRE(result == "ABCFGHDIKE");
        REQUIRE(*erased_result == "K");
    }

    /*multiple nodes*/ {
        auto node{find_node(f, "D")};
        REQUIRE(*node == "D");
        auto erased_result = f.erase(leading_of(node), std::next(trailing_of(node)));
        std::string result{to_string(preorder_range(f))};
        REQUIRE(result == "ABCFGHE");
        REQUIRE(*erased_result == "E");
    }
}

/**************************************************************************************************/

TEST_CASE("construction") {
    auto f{big_test_forest()};

    /* copy construction */ {
        auto f2{copy(f)};
        REQUIRE(f2 == f);
    }

    /* move construction */ {
        auto f_size{f.size()};
        auto f2 = std::move(f);
        REQUIRE(f2 != f);
        REQUIRE(f2.size() == f_size);
    }
}

/**************************************************************************************************/

TEST_CASE("assignment") {
    auto f{big_test_forest()};

    /* copy assignment */ {
        decltype(f) f2;
        REQUIRE(f2.empty());
        f2 = f;
        REQUIRE(f2 == f);
    }

    /* move assignment */ {
        auto f_size{f.size()};
        decltype(f) f2;
        f2 = std::move(f);
        REQUIRE(f2 != f);
        REQUIRE(f2.size() == f_size);
    }

    /* self-move assignment */ {
        f = big_test_forest();
        auto* pf{&f}; // We use a pointer here to get around a clang error when moving to self.
        auto f_size{f.size()};
        f = std::move(*pf);
        REQUIRE(f.size() == f_size);
    }
}

/**************************************************************************************************/

TEST_CASE("swap") {
    auto f1{big_test_forest()};
    auto f2{f1};
    f2.pop_back();
    f2.pop_front();
    auto f1_sz{f1.size()};
    auto f2_sz{f2.size()};

    REQUIRE(f1_sz != f2_sz);
    REQUIRE(f1 != f2);

    std::swap(f1, f2);

    REQUIRE(f2.size() == f1_sz);
    REQUIRE(f1.size() == f2_sz);

    f1.swap(f2);

    REQUIRE(f1.size() == f1_sz);
    REQUIRE(f2.size() == f2_sz);
}

/**************************************************************************************************/

TEST_CASE("test_equal_shape") {
    auto f1{big_test_forest()};
    auto f2{f1};

    for (auto& x : preorder_range(f2)) {
        x = "X";
    }

    REQUIRE(f1 != f2);
    REQUIRE(forests::equal_shape(f1, f2));
    REQUIRE(to_string(f2) == "XXXXXXXXXXXXXXXXXXXXXX");
}

/**************************************************************************************************/

TEST_CASE("test_transcribe_forest") {
    auto f1{big_test_forest()};
    stlab::forest<std::size_t> f2;

    forests::transcribe(f1, forests::transcriber(f2), [](const std::string& x) {
        assert(!x.empty());
        return static_cast<std::size_t>(x.front());
    });

    REQUIRE(forests::equal_shape(f1, f2));
    REQUIRE(to_string(f2) == "65666770707171727267687373747475756869696665");
}

/**************************************************************************************************/

TEST_CASE("test_flatten") {
    auto f1{big_test_forest()};
    std::vector<std::optional<std::string>> flat;

    forests::flatten(f1.begin(), f1.end(), std::back_inserter(flat));

    REQUIRE(to_string(flat) == "ABCF?G?H??DI?J?K??E???");

    decltype(f1) f2;

    forests::unflatten(flat.begin(), flat.end(), f2);

    REQUIRE(f1 == f2);
}

/**************************************************************************************************/
