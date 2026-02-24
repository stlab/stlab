#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include <stlab/concurrency/await.hpp>
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/immediate_executor.hpp>
#include <stlab/concurrency/serial_queue.hpp>

/**************************************************************************************************/

using namespace stlab;

/**************************************************************************************************/

inline void rest() {
    invoke_waiting([] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
}

/**************************************************************************************************/

void test0(stlab::schedule_mode mode) {
    std::vector<std::string> output;
    std::mutex m;
    serial_queue_t const a(stlab::default_executor, mode);
    serial_queue_t const b(stlab::default_executor, mode);
    serial_queue_t const c(stlab::default_executor, mode);
    serial_queue_t const d(stlab::default_executor, mode);
    auto aq(a.executor());
    auto bq(b.executor());
    auto cq(c.executor());
    auto dq(d.executor());

    auto strout([&](std::string str) {
        std::scoped_lock<std::mutex> const l(m);
        output.emplace_back(std::move(str));
    });

    aq([&]() noexcept { strout("a1                      ( 1)"); });

    bq([&]() noexcept { strout("   b1                   ( 2)"); });

    dq([&]() noexcept { strout("           d1           ( 3)"); });

    b([&]() {
        strout("   b2                   ( 4)");
    }).then(stlab::immediate_executor, [&]() {
          strout("   b2.1                 ( 4.1)");
      }).detach();

    cq([&]() noexcept { strout("        c1              ( 5)"); });

    cq([&]() noexcept { strout("        c2              ( 6)"); });

    dq([&]() noexcept { strout("           d2           ( 7)"); });

    cq([&]() noexcept { strout("        c3              ( 8)"); });

    bq([&]() noexcept { strout("   b3                   ( 9)"); });

    aq([&]() noexcept { strout("a2                      (10)"); });

    aq([&]() noexcept { strout("a3                      (11)"); });

    dq([&]() noexcept { strout("           d3           (12)"); });

    while (true) {
        {
            std::scoped_lock<std::mutex> const l(m);

            if (output.size() == 13) {
                break;
            }
        }
        rest();
    }

    for (const auto& s : output) {
        std::cout << s << '\n';
    }
}

/**************************************************************************************************/

inline auto str_hash(const std::string& x) -> std::uint64_t {
    std::uint64_t result(0xcbf29ce484222325);

    for (auto c : x) {
        result = (result ^ static_cast<std::uint64_t>(c)) * 0x100000001b3;
    }

    return result;
}

/**************************************************************************************************/

inline auto hash_combine(std::uint64_t hash, const std::string& x) -> std::uint64_t {
    return hash ^ (str_hash(x) << 1);
}

/**************************************************************************************************/

class test_hash_t {
    std::string _name;
    std::uint64_t _h{0};

    void confirm(std::uint64_t expected) {
        if (_h == expected) {
            return;
        }

        static std::mutex m;
        std::scoped_lock<std::mutex> const l(m);

        std::cout << std::hex << _name << " need: " << "0x" << expected << " have: " << "0x" << _h
                  << "\n"
                  << std::dec;
    }

public:
    test_hash_t(std::string name, std::uint64_t h) : _name(std::move(name)), _h(str_hash(_name)) {
        confirm(h);
    }

    void operator()(const std::string& s, std::uint64_t e) {
        _h = hash_combine(_h, s);

        confirm(e);
    }
};

/**************************************************************************************************/

struct serial_hash {
    serial_queue_t _q;
    std::atomic<int> _c{0};
    test_hash_t _h;

    explicit serial_hash(std::string s, std::uint64_t e, stlab::schedule_mode mode) :
        _q{stlab::default_executor, mode}, _h(std::move(s), e) {}

    void operator()(std::string s, std::uint64_t e) {
        _q.executor()([this, _e = e, _s = std::move(s)]() noexcept {
            _h(_s, _e);
            ++_c;
        });
    }
};

/**************************************************************************************************/

void test1(stlab::schedule_mode mode) {
    serial_hash a("a", 0xaf63dc4c8601ec8c, mode);
    serial_hash b("b", 0xaf63df4c8601f1a5, mode);
    serial_hash c("c", 0xaf63de4c8601eff2, mode);
    serial_hash d("d", 0xaf63d94c8601e773, mode);

    a("1", 0xf1a484d58a02d974);
    b("1", 0xf1a487d58a02c45d);
    d("1", 0xf1a481d58a02d28b);
    b("2", 0xaf63d94c86018477);
    c("1", 0xf1a486d58a02da0a);
    c("2", 0xaf63d84c86019a20);
    d("2", 0xaf63df4c860192a1);
    c("3", 0xf1a484d58a02a6e4);
    b("3", 0xf1a485d58a02b8b3);
    a("2", 0xaf63da4c8601995e);
    a("3", 0xf1a486d58a02a59a);
    d("3", 0xf1a483d58a02ae65);

    while (a._c != 3) {
        rest();
    }
    while (b._c != 3) {
        rest();
    }
    while (c._c != 3) {
        rest();
    }
    while (d._c != 3) {
        rest();
    }
}

/**************************************************************************************************/

TEST_CASE("serial_queue_interface") {
    test0(stlab::schedule_mode::single);

    std::cout << "-=-=-=-=-\n";

    test0(stlab::schedule_mode::all);

    std::cout << "-=-=-=-=-\n";

    test1(stlab::schedule_mode::single);

    std::cout << "-=-=-=-=-\n";

    test1(stlab::schedule_mode::all);
}

/**************************************************************************************************/
