#include <stlab/concurrency/default_executor.hpp>
#include <stlab/pre_exit.hpp>

#include <csignal>
#include <cstdlib>
#include <exception>

#if defined(_MSC_VER)
#include <crtdbg.h>
#include <stdlib.h> // NOLINT(modernize-deprecated-headers): Windows CRT extensions.
#endif

/// Verifies executor submission after completed pre-exit is diagnosed even before initial use.
int main() {
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    if (std::signal(SIGABRT, [](int) { std::_Exit(0); }) == SIG_ERR) std::_Exit(1);
    std::set_terminate([] { std::_Exit(0); });
    stlab::pre_exit();
    stlab::default_executor([]() noexcept {});
    std::_Exit(1);
}
