/**
 * @file stlab_groups.hpp
 * @brief Directory-level module groups for Doxygen (INPUT only; not compiled).
 */

/** @defgroup stlab_algorithm algorithm
 *  @brief Headers under @c stlab/algorithm/ .
 *
 *  @details
 *  Algorithms that complement the standard library, including utilities for intrusive iterator
 *  patterns used by @ref stlab_forest.
 */

/** @defgroup stlab_concurrency concurrency
 *  @brief Headers under @c stlab/concurrency/ .
 *
 *  @details
 *  Abstractions for multi-core algorithms with less contention: **futures**, **channels**,
 *  serial queues, and related utilities. Tasks, executors, timers, and process lifecycle APIs
 *  belong to the public dependency stlab-execution, using their original header paths.
 *
 *  `stlab::future` differs from `std::future` in several ways:
 *  - Continuations (`then`, `recover`) and combinators (`when_all`, `when_any`).
 *  - Multiple continuations from the same future when the value type is copyable.
 *  - Cancellation of uniquely contributing work when a `future` or `packaged_task` is destroyed
 *    before completion.
 *  - Custom **executors** and automatic flattening of `future<future<T>>` to `future<T>`.
 *
 *  Executor/backend contracts are documented by stlab-execution, not this package.
 *
 *  **Tooling:** building tests uses CMake and doctest. **Contributors** include Sean Parent,
 *  Foster Brereton, Felix Petriconi, and others.
 */

/** @defgroup stlab_iterator iterator
 *  @brief Headers under @c stlab/iterator/ .
 *
 *  @details
 *  Concepts and unsafe intrusive-list iterator helpers shared by @ref stlab_algorithm_reverse and
 *  @ref stlab_forest.
 */

/** @defgroup stlab_test test
 *  @brief Headers under @c stlab/test/ .
 *
 *  @details
 *  Utilities and **model types** for unit tests and for illustrating object lifetime, copying,
 *  moving, and comparison behavior (see @ref stlab_test_model).
 */
