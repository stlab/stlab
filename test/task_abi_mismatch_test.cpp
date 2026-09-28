/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#include <stlab/concurrency/task.hpp>

/// References an intentionally unavailable task storage ABI guard specialization.
int main() {
    using mismatched_guard =
        stlab::detail::task_storage_abi_guard<stlab::stlab_v2_task_storage_size + 1,
                                              stlab::stlab_v2_task_storage_alignment>;
    auto* volatile guard = &mismatched_guard::value;
    return guard == nullptr;
}
