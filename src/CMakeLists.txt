target_sources(stlab PRIVATE pre_exit.cpp concurrency/default_executor.cpp)
target_include_directories(stlab PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include ${PROJECT_BINARY_DIR}/include)
target_compile_definitions(stlab PRIVATE $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>)

# Apply ASan to the library when requested so it matches test executables (avoids LNK2038 annotation mismatch).
if(STLAB_SANITIZER STREQUAL "address")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(stlab PRIVATE /fsanitize=address)
    target_link_options(stlab PRIVATE /INCREMENTAL:NO)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(stlab PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(stlab PRIVATE -fsanitize=address)
  endif()
endif()

if(BUILD_SHARED_LIBS)
    set_target_properties(stlab PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()
