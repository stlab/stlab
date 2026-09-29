/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#include <stlab/concurrency/main_executor.hpp>
#include <stlab/concurrency/task.hpp>
#include <stlab/config.hpp>

#include <QtGlobal>
#if (STLAB_MAIN_EXECUTOR(QT5) &&                                                                \
         (QT_VERSION < QT_VERSION_CHECK(5, 0, 0) || QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)) || \
     STLAB_MAIN_EXECUTOR(QT6) &&                                                                \
         (QT_VERSION < QT_VERSION_CHECK(6, 0, 0) || QT_VERSION >= QT_VERSION_CHECK(7, 0, 0)))
#error "Mismatching Qt versions"
#endif
#include <QCoreApplication>
#include <QEvent>
#include <QObject>

#include <cassert>
#include <cstdlib>
#include <memory>
#include <utility>

namespace stlab {
STLAB_VERSION_NAMESPACE_BEGIN()
namespace detail {
namespace {

using main_task_t = task<void() noexcept>;

/// Receives main-executor events on the application thread.
struct event_receiver : QObject {
    /// Runs the task carried by a main-executor event.
    ///
    /// - Postcondition: returns `true` iff `event` was a main-executor event.
    auto event(QEvent* event) -> bool override;
};

/// Posted Qt event that owns one main-executor task and the receiver that runs it.
class executor_event : public QEvent {
    main_task_t _task;
    std::unique_ptr<event_receiver> _receiver;

public:
    /// Constructs an event owning `task`, with a receiver living on the application thread.
    ///
    /// - Precondition: a `QCoreApplication` instance exists.
    explicit executor_event(main_task_t task) :
        QEvent(QEvent::User), _task(std::move(task)),
        _receiver(std::make_unique<event_receiver>()) {
        _receiver->moveToThread(QCoreApplication::instance()->thread());
    }

    /// Invokes the owned task.
    void execute() noexcept { _task(); }

    /// Returns the object the event must be posted to.
    [[nodiscard]] auto receiver() const -> QObject* { return _receiver.get(); }
};

auto event_receiver::event(QEvent* event) -> bool {
    auto* main_event = dynamic_cast<executor_event*>(event);
    if (!main_event) return false;
    main_event->execute();
    return true;
}

} // namespace
} // namespace detail
STLAB_VERSION_NAMESPACE_END()

inline namespace v2 {

/// Posts one task to the Qt application event loop.
extern "C" void stlab_v2_main_executor_submit(const unsigned char* /*task_abi_guard*/,
                                              const stlab_v2_task_concept* vtable,
                                              stlab_v2_task_proc invoke,
                                              void* source) noexcept {
    assert(vtable != nullptr && invoke != nullptr && "Task vtable/invoke must not be null.");
    assert(QCoreApplication::instance() && "main_executor requires a QCoreApplication.");
    using namespace STLAB_VERSION_NAMESPACE()::detail;
    auto event = std::make_unique<executor_event>(main_task_t{vtable, invoke, source});
    auto* receiver = event->receiver();
    QCoreApplication::postEvent(receiver, event.release());
}

/// Runs the Qt application event loop and exits the process with its result; never returns.
extern "C" [[noreturn]] void stlab_v2_main_executor_run() {
    assert(QCoreApplication::instance() && "main_executor_run() requires a QCoreApplication.");
    std::exit(QCoreApplication::exec());
}

} // namespace v2
} // namespace stlab
