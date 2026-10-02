include(stlab/development)

get_target_property(stlab_public_dependencies stlab INTERFACE_LINK_LIBRARIES)
if(NOT "$<BUILD_INTERFACE:stlab::execution>" IN_LIST stlab_public_dependencies OR
   NOT "$<INSTALL_INTERFACE:stlab::$<1:execution>>" IN_LIST stlab_public_dependencies)
  message(FATAL_ERROR "STLab must publicly depend on stlab::execution in builds and installs.")
endif()
get_target_property(stlab_core_type stlab-core TYPE)
if(NOT stlab_core_type STREQUAL "INTERFACE_LIBRARY")
  message(FATAL_ERROR "stlab-core must be an INTERFACE compatibility target, not another runtime.")
endif()
get_target_property(stlab_core_dependencies stlab-core INTERFACE_LINK_LIBRARIES)
if(NOT "$<BUILD_INTERFACE:stlab::execution>" IN_LIST stlab_core_dependencies OR
   NOT "$<INSTALL_INTERFACE:stlab::$<1:execution>>" IN_LIST stlab_core_dependencies)
  message(FATAL_ERROR "The compatibility target must refer to execution in builds and installs.")
endif()

function(stlab_copy_runtime_dlls target_name)
  if(WIN32)
    add_custom_command(TARGET ${target_name} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:${target_name}>
        $<TARGET_RUNTIME_DLLS:${target_name}>
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND NOT STLAB_EMSCRIPTEN_PTHREADS)
  add_executable(stlab.test.cooperative_shutdown cooperative_shutdown_test.cpp
    main_executor_test_host.hpp)
  target_link_libraries(stlab.test.cooperative_shutdown PRIVATE stlab::stlab)
  add_test(NAME stlab.test.cooperative_shutdown_future_chain
    COMMAND stlab.test.cooperative_shutdown future_chain)
  set_tests_properties(stlab.test.cooperative_shutdown_future_chain PROPERTIES TIMEOUT 60)

  add_executable(stlab.test.emscripten_cooperative emscripten_cooperative_test.cpp)
  target_link_libraries(stlab.test.emscripten_cooperative PRIVATE stlab::stlab)
  add_test(NAME stlab.test.emscripten_cooperative COMMAND stlab.test.emscripten_cooperative)
  if(NOT STLAB_NO_STD_COROUTINES)
    add_test(NAME stlab.test.emscripten_coroutine
      COMMAND stlab.test.emscripten_cooperative coroutine)
    set_tests_properties(stlab.test.emscripten_coroutine PROPERTIES TIMEOUT 60)
  endif()
  add_test(NAME stlab.test.emscripten_await_terminate
    COMMAND ${CMAKE_COMMAND} "-Demulator=${CMAKE_CROSSCOMPILING_EMULATOR}"
      "-Dexecutable=$<TARGET_FILE:stlab.test.emscripten_cooperative>"
      -Dscenario=await -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_emscripten_terminate.cmake)
  add_test(NAME stlab.test.emscripten_waiting_terminate
    COMMAND ${CMAKE_COMMAND} "-Demulator=${CMAKE_CROSSCOMPILING_EMULATOR}"
      "-Dexecutable=$<TARGET_FILE:stlab.test.emscripten_cooperative>"
      -Dscenario=invoke_waiting -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_emscripten_terminate.cmake)
  set_tests_properties(stlab.test.emscripten_cooperative stlab.test.emscripten_await_terminate
    stlab.test.emscripten_waiting_terminate PROPERTIES TIMEOUT 60)
  return()
endif()

add_executable(stlab.test.execution_dependency execution_dependency_test.cpp)
target_link_libraries(stlab.test.execution_dependency PRIVATE stlab::stlab)
add_test(NAME stlab.test.execution_dependency COMMAND stlab.test.execution_dependency)
set_tests_properties(stlab.test.execution_dependency PROPERTIES TIMEOUT 60)
stlab_copy_runtime_dlls(stlab.test.execution_dependency)

# Independently generated configurations must compose without sharing coroutine policy.
if(NOT CMAKE_CROSSCOMPILING AND NOT stlab_execution_imported)
  add_test(NAME stlab.config.execution_coexistence
    COMMAND ${CMAKE_COMMAND}
      "-DSTLAB_SOURCE:PATH=${PROJECT_SOURCE_DIR}"
      "-DEXECUTION_SOURCE:PATH=${stlab_execution_source}"
      "-DTOOLKIT_SOURCE:PATH=${cpp-library_SOURCE_DIR}"
      "-DTEST_BINARY:PATH=${CMAKE_CURRENT_BINARY_DIR}/execution_coexistence"
      "-DTEST_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}"
      "-DTEST_MSVC:BOOL=${MSVC}"
      "-DTEST_GENERATOR=${CMAKE_GENERATOR}"
      "-DTEST_MAKE_PROGRAM:FILEPATH=${CMAKE_MAKE_PROGRAM}"
      "-DTEST_CACHE:PATH=${CPM_SOURCE_CACHE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/execution_config_coexistence.cmake")
  set_tests_properties(stlab.config.execution_coexistence PROPERTIES TIMEOUT 450)
endif()

add_library(testing INTERFACE)
add_library(stlab::testing ALIAS testing)
target_link_libraries(testing INTERFACE doctest::doctest stlab::development stlab::stlab)

add_executable(stlab.test.channel
  channel_functor_tests.cpp
  channel_merge_round_robin_tests.cpp
  channel_merge_unordered_tests.cpp
  channel_merge_zip_with_tests.cpp
  channel_process_tests.cpp
  channel_test_helper.cpp
  channel_tests.cpp
  main.cpp
  channel_test_helper.hpp)

add_executable(stlab.test.executor executor_test.cpp main.cpp)
add_executable(stlab.test.future
  future_recover_tests.cpp
  future_tests.cpp
  future_then_tests.cpp
  future_when_all_arguments_tests.cpp
  future_when_all_range_tests.cpp
  future_when_any_arguments_tests.cpp
  future_when_any_range_tests.cpp
  main.cpp
  cooperative_executor.hpp
  future_test_helper.hpp
  future_reduction_tests.cpp)
if(NOT STLAB_NO_STD_COROUTINES)
  target_sources(stlab.test.future PUBLIC future_coroutine_tests.cpp)
endif()

add_executable(stlab.test.serial_queue serial_queue_test.cpp main.cpp)
add_executable(stlab.test.system_timer system_timer_test.cpp main.cpp)
add_executable(stlab.test.tuple_algorithm tuple_algorithm_test.cpp main.cpp)
add_executable(stlab.test.cow cow_test.cpp main.cpp)
add_executable(stlab.test.task task_test.cpp main.cpp)
add_executable(stlab.test.tuple tuple_test.cpp main.cpp)
add_executable(stlab.test.traits traits_test.cpp main.cpp)
add_executable(stlab.test.forest forest_test.cpp main.cpp)
add_executable(stlab.test.utility utility_test.cpp main.cpp)

# Disable GCC's existing false positive; see github.com/stlab/stlab/issues/438.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "11")
  set_source_files_properties(cow_test.cpp PROPERTIES COMPILE_FLAGS -Wno-free-nonheap-object)
endif()

foreach(component IN ITEMS channel executor future serial_queue system_timer tuple_algorithm
    cow task tuple traits forest utility)
  set(target stlab.test.${component})
  if(NOT component STREQUAL "forest" AND NOT component STREQUAL "utility")
    target_compile_definitions(${target} PRIVATE STLAB_UNIT_TEST)
  endif()
  target_link_libraries(${target} PUBLIC stlab::testing)
  add_test(NAME ${target} COMMAND ${target})
  stlab_copy_runtime_dlls(${target})
endforeach()

# Integration coverage of futures/await with a shared portable execution runtime.
if(WIN32 AND STLAB_CORE_SHARED AND STLAB_TASK_SYSTEM STREQUAL "portable")
  add_executable(stlab.test.portable_shared_smoke portable_shared_smoke_test.cpp main.cpp)
  target_compile_definitions(stlab.test.portable_shared_smoke PRIVATE STLAB_UNIT_TEST)
  target_link_libraries(stlab.test.portable_shared_smoke PUBLIC stlab::testing)
  add_test(NAME stlab.test.portable_shared_smoke COMMAND stlab.test.portable_shared_smoke)
  stlab_copy_runtime_dlls(stlab.test.portable_shared_smoke)
endif()

set_target_properties(
  stlab.test.channel stlab.test.future stlab.test.serial_queue stlab.test.cow
  stlab.test.task stlab.test.tuple stlab.test.traits PROPERTIES CXX_EXTENSIONS OFF)

include(ProcessorCount)
ProcessorCount(nProcessors)
if(nProcessors)
  set_tests_properties(
    stlab.test.channel stlab.test.executor stlab.test.future stlab.test.serial_queue
    stlab.test.cow stlab.test.task stlab.test.tuple PROPERTIES PROCESSORS ${nProcessors})
endif()
