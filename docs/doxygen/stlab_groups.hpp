/**
 * @file stlab_groups.hpp
 * @brief Root and directory-level module groups for Doxygen (INPUT only; not compiled).
 */

/** @defgroup stlab stlab
 *  @brief Public API grouped by header (nested under directory modules where applicable).
 *
 *  @details
 *  Source repository: [stlab/stlab on GitHub](https://github.com/stlab/stlab).
 *  License: [Boost Software License 1.0](https://www.boost.org/LICENSE_1_0.txt).
 */

/** @defgroup stlab_algorithm algorithm
 *  @ingroup stlab
 *  @brief Headers under @c stlab/algorithm/ .
 *
 *  @details
 *  Algorithms that complement the standard library, including utilities for intrusive iterator
 *  patterns used by @ref stlab_forest.
 */

/** @defgroup stlab_concurrency concurrency
 *  @ingroup stlab
 *  @brief Headers under @c stlab/concurrency/ .
 *
 *  @details
 *  Abstractions for multi-core algorithms with less contention: **futures**, **channels**,
 *  executors, and related utilities.
 *
 *  `stlab::future` differs from `std::future` in several ways:
 *  - Continuations (`then`, `recover`) and combinators (`when_all`, `when_any`).
 *  - Multiple continuations from the same future when the value type is copyable.
 *  - Cancellation of uniquely contributing work when a `future` or `packaged_task` is destroyed
 *    before completion.
 *  - Custom **executors** and automatic flattening of `future<future<T>>` to `future<T>`.
 *
 *  A **default executor** uses the system thread pool when the platform provides one; otherwise
 *  a portable task-stealing implementation is used (see @ref stlab_concurrency_default_executor).
 *
 *  **Tooling:** building tests uses CMake and doctest. **Contributors** include Sean Parent,
 *  Foster Brereton, Felix Petriconi, and others.
 */

/** @defgroup stlab_iterator iterator
 *  @ingroup stlab
 *  @brief Headers under @c stlab/iterator/ .
 *
 *  @details
 *  Concepts and unsafe intrusive-list iterator helpers shared by @ref stlab_algorithm_reverse and
 *  @ref stlab_forest.
 */

/** @defgroup stlab_test test
 *  @ingroup stlab
 *  @brief Headers under @c stlab/test/ .
 *
 *  @details
 *  Utilities and **model types** for unit tests and for illustrating object lifetime, copying,
 *  moving, and comparison behavior (see @ref stlab_test_model).
 */
