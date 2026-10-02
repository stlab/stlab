# Distributed under the Boost Software License, Version 1.0.
# Narrow source-build regression for independently generated configuration headers.

function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  file(APPEND "${TEST_BINARY}/commands.log"
    "Command: ${ARGV}\nExit: ${result}\n${output}\n${error}\n")
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Command (${result}): ${ARGV}\n${output}\n${error}")
  endif()
  # Preserve configuration warnings in verbose CTest output.
  if(NOT error STREQUAL "")
    message("${error}")
  endif()
endfunction()

set(common
  -G "${TEST_GENERATOR}"
  "-DCMAKE_MAKE_PROGRAM:FILEPATH=${TEST_MAKE_PROGRAM}"
  "-DCMAKE_CXX_COMPILER:FILEPATH=${TEST_COMPILER}"
  "-DCPM_cpp-library_SOURCE:PATH=${TOOLKIT_SOURCE}"
  "-DCPM_SOURCE_CACHE:PATH=${TEST_CACHE}"
  -DBUILD_TESTING=OFF -DSTLAB_EXECUTION_INSTALL=OFF
  -DCMAKE_BUILD_TYPE=Debug)
if(TEST_MSVC)
  set(compile_flags /nologo /Zs /EHsc /W4 /WX /DNOMINMAX)
  set(include_flag /I)
  set(define_flag /D)
  set(standard_flag /std:c++)
else()
  set(compile_flags -fsyntax-only -Wall -Wextra -Werror)
  set(include_flag -I)
  set(define_flag -D)
  set(standard_flag -std=c++)
endif()

# Recreate only this test's child directories, so stale option caches cannot mask ownership.
file(REMOVE_RECURSE "${TEST_BINARY}")
file(MAKE_DIRECTORY "${TEST_BINARY}")
foreach(execution_standard IN ITEMS 17 20)
  set(execution_binary "${TEST_BINARY}/execution-${execution_standard}")
  run("${CMAKE_COMMAND}" -S "${EXECUTION_SOURCE}" -B "${execution_binary}"
    ${common} "-DCMAKE_CXX_STANDARD=${execution_standard}")
  run("${CMAKE_COMMAND}" --build "${execution_binary}" --target execution)
  file(READ "${execution_binary}/include/stlab/execution/config.hpp" execution_config)
  file(READ "${execution_binary}/CMakeCache.txt" execution_cache)
  if(execution_config MATCHES "STLAB_STD_COROUTINES" OR
     execution_cache MATCHES "STLAB_NO_STD_COROUTINES")
    message(FATAL_ERROR "Standalone execution must not own STLab coroutine configuration.")
  endif()
  # Even execution configured/built as C++20 must allow a C++17 header consumer.
  run("${TEST_COMPILER}" ${compile_flags} "${standard_flag}17"
    "${include_flag}${execution_binary}/include" "${include_flag}${EXECUTION_SOURCE}/include"
    "${EXECUTION_SOURCE}/test/header_smoke.cpp")
endforeach()

foreach(stlab_standard IN ITEMS 17 20)
  foreach(no_coroutines IN ITEMS OFF ON)
    set(stlab_binary "${TEST_BINARY}/stlab-${stlab_standard}-${no_coroutines}")
    run("${CMAKE_COMMAND}" -S "${STLAB_SOURCE}" -B "${stlab_binary}"
      ${common} "-DCPM_stlab-execution_SOURCE:PATH=${EXECUTION_SOURCE}"
      "-DCMAKE_CXX_STANDARD=${stlab_standard}" "-DSTLAB_NO_STD_COROUTINES=${no_coroutines}"
      -DSTLAB_INSTALL=OFF)
    run("${CMAKE_COMMAND}" --build "${stlab_binary}" --target stlab)
    set(expected 0)
    if(stlab_standard EQUAL 20 AND NOT no_coroutines)
      set(expected 1)
    endif()
    foreach(execution_standard IN ITEMS 17 20)
      foreach(stlab_first IN ITEMS 0 1)
        message(STATUS "execution C++${execution_standard} / STLab C++${stlab_standard} "
          "STLAB_NO_STD_COROUTINES=${no_coroutines}, STLab first=${stlab_first}, expected=${expected}")
        run("${TEST_COMPILER}" ${compile_flags} "${standard_flag}${stlab_standard}"
          "${define_flag}TEST_STLAB_FIRST=${stlab_first}"
          "${define_flag}TEST_EXPECT_COROUTINES=${expected}"
          "${include_flag}${stlab_binary}/include" "${include_flag}${STLAB_SOURCE}/include"
          "${include_flag}${TEST_BINARY}/execution-${execution_standard}/include"
          "${include_flag}${EXECUTION_SOURCE}/include"
          "${STLAB_SOURCE}/test/execution_config_coexistence.cpp")
      endforeach()
    endforeach()
  endforeach()
endforeach()
