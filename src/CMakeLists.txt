target_sources(stlab PRIVATE stlab.cpp)

# Match test instrumentation without adding a second execution runtime.
if(STLAB_SANITIZER STREQUAL "address")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(stlab PRIVATE /fsanitize=address)
    target_link_options(stlab PRIVATE /INCREMENTAL:NO)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(stlab PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(stlab PRIVATE -fsanitize=address)
  endif()
endif()
