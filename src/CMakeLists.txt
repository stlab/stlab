target_sources(stlab PRIVATE stlab.cpp)
target_sources(stlab-core PRIVATE pre_exit.cpp concurrency/executor_abi.cpp)
target_include_directories(stlab-core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${PROJECT_BINARY_DIR}/include)
target_compile_definitions(stlab-core PRIVATE $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>)

# Apply ASan to the library when requested so it matches test executables (avoids LNK2038 annotation mismatch).
if(STLAB_SANITIZER STREQUAL "address")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(stlab-core PRIVATE /fsanitize=address)
    target_link_options(stlab-core PRIVATE /INCREMENTAL:NO)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(stlab-core PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(stlab-core PRIVATE -fsanitize=address)
  endif()
endif()

if(STLAB_CORE_SHARED AND WIN32)
  target_sources(stlab-core PRIVATE stlab.def)
endif()
