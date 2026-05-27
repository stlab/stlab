# STLab to-do / follow-ups

## Complete migration to doxygen
- Ensure that all code is documented and building with missing documentation as an error.
- Review all documentation to ensure it is a specification and does not contain unnecessary implementation details.
- Validate links (some auto-links are broken).

## High-level goals
- Move the library to use stlab/cpp-library (improving cpp-library as necessary).
- Separate out the non-doxygen (jekyll) documentation to a separate repo.
- Refactor libraries into individual repos (like enum-ops and copy-on-write).
    - When factoring out forest libraries consolidation with stlab/adobe_source_libraries forest library.

## Library improvements
- When we can drop C++20 support see if we can build a more efficient future<> merging the shared state with the coroutine handle state.
- Completely rework channels to be C++20 coroutine based.

## Factor out the default_executor into a Rust library (see open PR on this)
- Make a reusable Rust library and the core implementation for stlab C++ library. (Check with Ps team on status of using Rust before making this move.)

## Testing
- Ensure that every header file has a corresponding test file that includes the header file first.
- Review tests to ensure every API has a corresponding test.