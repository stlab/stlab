---
description: Documentation by contract (Better Code ch.2) using Doxygen and /// brief comments
globs: "**/*.hpp,**/*.h,**/*.cpp"
alwaysApply: false
---

# Documentation by Contract (Doxygen)

Follow *programming by contract* as in [Better Code — Contracts](https://github.com/stlab/better-code/blob/main/better-code/src/chapter-2-contracts.md). Treat the contract as the component’s interface: clients depend on it.

## Syntax

- **Brief / single-line**: Use `///` (Doxygen).
- **File header**: Use a Doxygen block `/** ... */` at the top of each file. Do not use a leading `*` on each line. Describe the **purpose** and **functionality** of the public API only (no `detail` or implementation-only names in the file description); treat it as a short tutorial (examples may be added separately).
- **Long descriptions**: Use `/** ... */` or `///` with `@param`, `@pre`, `@post`, `@return`, etc. as needed.
- **Markdown in descriptions**: Use markdown within comments for identifiers and code. Put parameter names, function names, and type names in backticks, e.g. `` `f` ``, `` `executor` ``, `` `is_ready()` ``.

## File header

Every file must start with a Doxygen block comment that states what the file is for, how its main types and functions behave, and how they work together—a short tutorial (no leading `*` on each line; add examples elsewhere if desired). Describe functionality that affects usage (e.g. cancellation on destruction, copyable vs move-only and multiple vs single continuation, when continuations run). The top-level description is for the **public API only**: do not mention or reference anything in the `detail` namespace or other implementation-only components; those may be documented in place next to their declarations.

```cpp
/**
Concurrency primitives: futures, executors, and channels.

Key types: `future<T>`, `promise<T>`, `channel<T>`. Use future/promise for
one-shot async results; use channel for producer-consumer streams. Tasks are
canceled when the future is destroyed. ...
*/
```

## Per-declaration contract

Document **every** declaration (types, functions, methods, non-obvious constants). Put the contract in comments next to the declaration.

1. **Summary**: One sentence fragment — what the thing *is* or *does* (and what it returns). End with a period. Use `///` for the summary line.
2. **Preconditions**: Only when not obvious from the summary. Use `@pre` or `- Precondition:` in Doxygen.
3. **Postconditions**: Usually fully described by the summary; add `@post` only when something important can’t be stated in the summary.
4. **Complexity**: Document for any operation that is not O(1) in time or space.

Omit preconditions/postconditions that are clearly implied by the summary (e.g. “Removes and returns the last element” implies “container is non-empty”).

## Examples

```cpp
/// Removes and returns the last element.
/// @pre The container is non-empty.  (omit if obvious from summary)
T pop_back();

/// Sorts the elements so that all adjacent pairs satisfy the ordering `comp`.
/// @pre `comp` defines a strict weak ordering over the elements.
/// Complexity: at most N log N comparisons.
template <typename It, typename Compare>
void sort(It first, It last, Compare comp);
```

```cpp
/// A resizable random-access sequence of elements of type T.
template <typename T>
class dynamic_array {
  /// The number of elements.  (invariant: non-negative)
  size_t size() const;
};
```

## Type invariants

For types with non-trivial state, document the **type invariant** (the condition that holds at the public API boundary). Use a short comment near the type or in the file header. Keep implementation-only invariants in non-public comments.

## Policies (summary)

- Every declaration has a documentation comment with at least a summary.
- Pre/postconditions that are implied by the summary need not be repeated.
- Document complexity for non-constant-time operations.
- File header: `/** ... */` with no leading `*` per line; describe public API only (no `detail`/implementation in the description), as a short tutorial (no inline examples).
- Use `///` for brief and single-line comments; Doxygen block `/** ... */` for file header and long descriptions.
- Use markdown backticks for parameter/function/type names in comments (e.g. `` `f` ``, `` `executor` ``).
