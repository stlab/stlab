/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_MAIN_EXECUTOR_HPP
#define STLAB_CONCURRENCY_MAIN_EXECUTOR_HPP

/*! @file main_executor.hpp
 *  @brief Executor for the application's main queue.
 *
 *  @details
 *  Tasks submitted to `main_executor` run in submission order on the main queue selected by
 *  `STLAB_MAIN_EXECUTOR` when `stlab-core` is built: the libdispatch main queue, the Qt
 *  application event loop, the Emscripten main runtime thread, or (opt-in) a portable
 *  stlab-owned queue. `main_executor_run()` services the main queue on the calling thread and
 *  never returns, like `dispatch_main()`; the program ends by calling `pre_exit()` and
 *  `std::exit()` from a task.
 *
 *  Windows has no process main queue (each UI thread owns its message queue), so no main executor
 *  is provided there unless `STLAB_MAIN_EXECUTOR` selects Qt or `portable`.
 */

#include <stlab/config.hpp>

#if STLAB_MAIN_EXECUTOR(PORTABLE) || STLAB_MAIN_EXECUTOR(LIBDISPATCH)
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/task.hpp>

#include <type_traits>
#include <utility>
#elif STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)
#include <QtGlobal>
#if (STLAB_MAIN_EXECUTOR(QT5) &&                                                                \
         (QT_VERSION < QT_VERSION_CHECK(5, 0, 0) || QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) || \
     STLAB_MAIN_EXECUTOR(QT6) &&                                                                \
         (QT_VERSION < QT_VERSION_CHECK(6, 0, 0) || QT_VERSION >= QT_VERSION_CHECK(7, 0, 0)))
#error "Mismatching Qt versions"
#endif
#include <QCoreApplication>
#include <QEvent>
#include <memory>
#include <stlab/concurrency/task.hpp>
#elif STLAB_MAIN_EXECUTOR(EMSCRIPTEN)
#include <stlab/concurrency/default_executor.hpp>
#endif

/**************************************************************************************************/

#if STLAB_MAIN_EXECUTOR(PORTABLE) || STLAB_MAIN_EXECUTOR(LIBDISPATCH)

namespace stlab {
inline namespace v2 {

/** @addtogroup stlab_concurrency_executor_abi
 *  @{
 */

/// Submits one task to the main queue.
///
/// - Precondition: `task_abi_guard` points to `detail::current_task_storage_abi_guard::value`.
/// - Precondition: `vtable` and `invoke` are not `nullptr`.
/// - Precondition: `source` is the `relocation_source()` of a live `task<void() noexcept>` sharing
///   `vtable`/`invoke`, valid for the duration of this call.
/// - Postcondition: exactly one invocation of the relocated target is scheduled on the main queue,
///   after every task previously submitted from the calling thread.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* task_abi_guard,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept;

/// Services the main queue on the calling thread; never returns.
///
/// - Precondition: called at most once per process, from the thread the platform designates as
///   main where it designates one.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() noexcept;

/** @} */

} // namespace v2
} // namespace stlab

#endif

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()

/** @defgroup stlab_concurrency_main_executor main_executor
 *  @ingroup stlab_concurrency
 *  @brief Main-thread / UI-thread executor (Qt, libdispatch, Emscripten, etc.).
 *  @{
 */

/**************************************************************************************************/

namespace detail {

/**************************************************************************************************/

#if STLAB_MAIN_EXECUTOR(PORTABLE) || STLAB_MAIN_EXECUTOR(LIBDISPATCH)

/// Executor that submits `void() noexcept` tasks to the main queue through the shared core ABI.
struct main_executor_type {
    using result_type = void;

    /// Schedules `f` to run on the main queue after every task previously submitted from the
    /// calling thread.
    template <class F>
    auto operator()(F&& f) const -> std::enable_if_t<std::is_nothrow_invocable_v<std::decay_t<F>>> {
        task<void() noexcept> t{std::forward<F>(f)};
        stlab_v2_main_executor_submit(&current_task_storage_abi_guard::value,
                                      t.relocation_concept(), t.relocation_invoke(),
                                      t.relocation_source());
    }
};

/**************************************************************************************************/

#elif STLAB_MAIN_EXECUTOR(QT5) || STLAB_MAIN_EXECUTOR(QT6)

class main_executor_type {
    using result_type = void;

    struct event_receiver;

    class executor_event : public QEvent {
        stlab::task<void()> _f;
        std::unique_ptr<event_receiver> _receiver;

    public:
        executor_event() : QEvent(QEvent::User), _receiver(new event_receiver()) {
            _receiver->moveToThread(QCoreApplication::instance()->thread());
        }

        template <typename F>
        void set_task(F&& f) {
            _f = std::forward<F>(f);
        }

        void execute() { _f(); }

        QObject* receiver() const { return _receiver.get(); }
    };

    struct event_receiver : public QObject {
        bool event(QEvent* event) override {
            auto myEvent = dynamic_cast<executor_event*>(event);
            if (myEvent) {
                myEvent->execute();
                return true;
            }
            return false;
        }
    };

public:
    template <typename F>
    auto operator()(F f) const -> std::enable_if_t<std::is_nothrow_invocable_v<F>> {
        auto event = std::make_unique<executor_event>();
        event->set_task(std::move(f));
        auto receiver = event->receiver();
        QCoreApplication::postEvent(receiver, event.release());
    }
};

/**************************************************************************************************/

#elif STLAB_MAIN_EXECUTOR(EMSCRIPTEN)

struct main_executor_type {
    using result_type = void;

    template <class F>
    auto operator()(F&& f) const -> std::enable_if_t<std::is_nothrow_invocable_v<F>> {
        using function_type = typename std::remove_reference<F>::type;
        auto p = new function_type(std::forward<F>(f));

        /*
          `emscripten_async_run_in_main_runtime_thread()` schedules a function to run on the main
           JS thread, however, the code can be executed at any POSIX thread cancellation point if
           wasm code is executing on the JS main thread.
           Executing the code from a POSIX thread cancellation point can cause problems, including
           deadlocks and data corruption. Consider:
           ```
               mutex.lock();   // <-- If reentered, would deadlock here
               new T;          // <-- POSIX cancellation point, could reenter
           ```
           The call to `emscripten_async_call()` bounces the call to execute as part of the main
           run-loop on the current (main) thread. This avoids nasty reentrancy issues if executed
           from a POSIX thread cancellation point.
       */

        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VI, static_cast<void (*)(void*)>([](void* f_) {
                emscripten_async_call(
                    [](void* f_) {
                        auto f = static_cast<function_type*>(f_);
                        // Note the absence of exception handling.
                        // Operations queued to the task system cannot throw as a precondition.
                        // We use packaged tasks to marshal exceptions.
                        (*f)();
                        delete f;
                    },
                    f_, 0);
            }),
            p);
    }
};

#elif STLAB_MAIN_EXECUTOR(NONE)

// For documentation only
struct main_executor_type {
    using result_type = void;

    template <typename F>
    void operator()(F f) const {}
};

#endif

} // namespace detail

/// Runs `void() noexcept` tasks in submission order on the configured main queue.
inline constexpr auto main_executor = detail::main_executor_type{};

#if STLAB_MAIN_EXECUTOR(PORTABLE) || STLAB_MAIN_EXECUTOR(LIBDISPATCH)
/// Services the main queue on the calling thread; never returns.
///
/// - Precondition: called at most once per process, from the thread the platform designates as
///   main where it designates one.
[[noreturn]] inline void main_executor_run() noexcept { stlab_v2_main_executor_run(); }
#endif

/**************************************************************************************************/

/** @} */

STLAB_VERSION_NAMESPACE_END()
} // namespace stlab

/**************************************************************************************************/

#endif // STLAB_CONCURRENCY_MAIN_EXECUTOR_HPP
