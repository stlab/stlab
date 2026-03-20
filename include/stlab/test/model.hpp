/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_MODEL_HPP
#define STLAB_MODEL_HPP

/**************************************************************************************************/

#include <stlab/config.hpp>

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include <stlab/concurrency/await.hpp>

/**************************************************************************************************/

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/**************************************************************************************************/

struct annotate_counters {
    std::atomic_size_t _dtor{0};
    std::atomic_size_t _copy_ctor{0};
    std::atomic_size_t _move_ctor{0};
    std::atomic_size_t _copy_assign_lhs{0};
    std::atomic_size_t _copy_assign_rhs{0};
    std::atomic_size_t _move_assign_lhs{0};
    std::atomic_size_t _move_assign_rhs{0};
    std::atomic_size_t _swap{0};
    std::atomic_size_t _equality{0};
    std::mutex _mutex;
    std::condition_variable _condition;

    auto remaining() const -> std::size_t { return _copy_ctor + _move_ctor - _dtor + 1; }

    void wait(std::size_t count) {
        stlab::invoke_waiting([&] {
            std::unique_lock<std::mutex> lock(_mutex);
            _condition.wait(lock, [&] { return count == remaining(); });
        });
    }

    friend inline auto operator<<(std::ostream& out, const annotate_counters& x) -> std::ostream& {
        out << "           dtor: " << x._dtor << "\n";
        out << "      copy_ctor: " << x._copy_ctor << "\n";
        out << "      move_ctor: " << x._move_ctor << "\n";
        out << "copy_assign_lhs: " << x._copy_assign_lhs << "\n";
        out << "copy_assign_rhs: " << x._copy_assign_rhs << "\n";
        out << "move_assign_lhs: " << x._move_assign_lhs << "\n";
        out << "move_assign_rhs: " << x._move_assign_rhs << "\n";
        out << "           swap: " << x._swap << "\n";
        out << "       equality: " << x._equality << "\n";

        return out;
    }
};

struct annotate {
    annotate_counters* _counters;
    explicit annotate(annotate_counters& counters) : _counters(&counters) {}

    ~annotate() {
        {
            ++_counters->_dtor;
            _counters->_condition.notify_one();
        }
    }

    annotate(const annotate& x) : _counters(x._counters) { ++_counters->_copy_ctor; }
    annotate(annotate&& x) noexcept : _counters(x._counters) { ++_counters->_move_ctor; }

    auto operator=(const annotate& x) -> annotate& {
        if (this == &x) return *this;
        ++x._counters->_copy_assign_rhs;
        ++_counters->_copy_assign_lhs;
        return *this;
    }
    auto operator=(annotate&& x) noexcept -> annotate& {
        ++x._counters->_move_assign_rhs;
        ++_counters->_move_assign_lhs;
        return *this;
    }

    friend inline void swap(annotate& x, annotate& y) noexcept {
        ++x._counters->_swap;
        ++y._counters->_swap;
    }

    friend inline auto operator==(const annotate& x, const annotate& y) -> bool {
        ++x._counters->_equality;
        ++y._counters->_equality;
        return true;
    }
    friend inline auto operator!=(const annotate& x, const annotate& y) -> bool {
        return !(x == y);
    }
};

/**************************************************************************************************/

struct regular {
    int _x;

    explicit regular(int x) : _x(x) { std::cout << _x << " ctor" << '\n'; }
    ~regular() { std::cout << _x << " dtor" << '\n'; }

    regular(const regular& rhs) : _x(rhs._x) { std::cout << _x << " copy-ctor" << '\n'; }
    regular(regular&& rhs) noexcept : _x(rhs._x) {
        std::cout << _x << " move-ctor" << '\n';
        rhs._x = 0;
    }

    auto operator=(const regular& rhs) -> regular& {
        std::cout << _x << " assign" << '\n';
        if (&rhs == this) return *this;
        _x = rhs._x;
        return *this;
    }
    auto operator=(regular&& rhs) noexcept -> regular& {
        std::cout << _x << " move-assign" << '\n';
        _x = rhs._x;
        rhs._x = 0;
        return *this;
    }

    friend inline void swap(regular& lhs, regular& rhs) noexcept {
        std::cout << lhs._x << "/" << rhs._x << " swap " << '\n';
        std::swap(lhs._x, rhs._x);
    }

    friend inline auto operator==(const regular& lhs, const regular& rhs) -> bool {
        return lhs._x == rhs._x;
    }
    friend inline auto operator!=(const regular& lhs, const regular& rhs) -> bool {
        return !(lhs == rhs);
    }

    friend inline auto operator<(const regular& lhs, const regular& rhs) -> bool {
        bool result(lhs._x < rhs._x);
        std::cout << lhs._x << " < " << rhs._x << ": " << std::boolalpha << result << '\n';
        return result;
    }
};

/**************************************************************************************************/

class move_only {
private:
    int _member{0};

public:
    move_only() = default;
    move_only(int member) : _member(member) {}
    move_only(const move_only&) = delete;
    auto operator=(const move_only&) -> move_only& = delete;
    move_only(move_only&&) = default;
    auto operator=(move_only&&) -> move_only& = default;
    virtual ~move_only() = default;

    auto member() -> int { return _member; }
    [[nodiscard]] auto member() const -> int { return _member; }

    friend auto operator==(const move_only& a, const move_only& b) -> bool {
        return a._member == b._member;
    }
    friend auto operator!=(const move_only& a, const move_only& b) -> bool { return !(a == b); }
};

/**************************************************************************************************/

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif

/**************************************************************************************************/
