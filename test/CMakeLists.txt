include(stlab/development)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  add_executable(stlab.test.emscripten_timer emscripten_timer_test.cpp)
  target_link_libraries(stlab.test.emscripten_timer PRIVATE stlab-core)
  add_test(NAME stlab.test.emscripten_timer COMMAND stlab.test.emscripten_timer)
  add_test(NAME stlab.test.emscripten_timer_cancel_pending
    COMMAND stlab.test.emscripten_timer cancel_pending)
  set_tests_properties(stlab.test.emscripten_timer stlab.test.emscripten_timer_cancel_pending
    PROPERTIES TIMEOUT 60)
  if(STLAB_EMSCRIPTEN_PTHREADS)
    add_test(NAME stlab.test.emscripten_timer_shutdown_worker
      COMMAND stlab.test.emscripten_timer shutdown_worker)
    set_tests_properties(stlab.test.emscripten_timer_shutdown_worker PROPERTIES TIMEOUT 60)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten" AND NOT STLAB_EMSCRIPTEN_PTHREADS)
  add_executable(stlab.test.main_executor_shutdown_order main_executor_shutdown_order_test.cpp)
  target_link_libraries(stlab.test.main_executor_shutdown_order PRIVATE stlab-core)
  add_test(NAME stlab.test.main_executor_shutdown_order COMMAND stlab.test.main_executor_shutdown_order)
  set_tests_properties(stlab.test.main_executor_shutdown_order PROPERTIES TIMEOUT 60)
  add_test(NAME stlab.test.emscripten_configuration
    COMMAND ${CMAKE_COMMAND} "-Dsource_dir=${PROJECT_SOURCE_DIR}"
      "-Dbinary_dir=${PROJECT_BINARY_DIR}/configuration-tests"
      "-Dem_config=${EM_CONFIG_EXECUTABLE}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/emscripten_config_test.cmake)
  set_tests_properties(stlab.test.emscripten_configuration PROPERTIES TIMEOUT 450)
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
  add_executable(stlab.test.main_executor_order main_executor_order_test.cpp)
  target_link_libraries(stlab.test.main_executor_order PRIVATE stlab-core)
  add_test(NAME stlab.test.main_executor_order COMMAND stlab.test.main_executor_order)
  set_tests_properties(stlab.test.emscripten_cooperative stlab.test.emscripten_await_terminate
    stlab.test.emscripten_waiting_terminate stlab.test.main_executor_order PROPERTIES TIMEOUT 60)
  return()
endif()

# test dependencies: doctest is fetched in root CMakeLists when BUILD_TESTING is ON
# Sanitizer options from development.cmake apply to test targets; stlab target gets them in src/CMakeLists.txt.

function(stlab_copy_runtime_dlls target_name)
  if(WIN32)
    add_custom_command(
      TARGET ${target_name}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:${target_name}> $<TARGET_RUNTIME_DLLS:${target_name}>
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()

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

target_compile_definitions(stlab.test.channel PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.channel PUBLIC stlab::testing)

add_test(
  NAME stlab.test.channel
  COMMAND stlab.test.channel
)


################################################################################
#
# Establish a convenience target to encapsulate the properties common to the
# stlab tests and establish an alias for uniformity.
#
add_library(testing INTERFACE)
add_library(stlab::testing ALIAS testing)

#
# CMake targets linking to the stlab::testing target will (transitively)
# link to doctest::doctest and to stlab::stlab target.
#

target_link_libraries(testing INTERFACE
  doctest::doctest
  stlab::development
  stlab::stlab)

add_executable(stlab.test.executor
  executor_test.cpp
  main.cpp)



target_compile_definitions(stlab.test.executor PRIVATE STLAB_UNIT_TEST)
target_link_libraries(stlab.test.executor PUBLIC stlab::testing)

add_test(
  NAME stlab.test.executor
  COMMAND stlab.test.executor
)

if(WIN32 AND STLAB_CORE_SHARED)
  add_executable(stlab.test.core_shared_smoke
    core_shared_smoke_test.cpp
    core_shared_smoke_main.cpp)

  target_compile_definitions(stlab.test.core_shared_smoke PRIVATE STLAB_UNIT_TEST)
  target_link_libraries(stlab.test.core_shared_smoke PUBLIC doctest::doctest stlab-core)

  add_test(
    NAME stlab.test.core_shared_smoke
    COMMAND stlab.test.core_shared_smoke
  )
endif()

if(WIN32 AND STLAB_CORE_SHARED AND STLAB_TASK_SYSTEM STREQUAL "portable")
  add_executable(stlab.test.portable_shared_smoke
    portable_shared_smoke_test.cpp
    main.cpp)

  target_compile_definitions(stlab.test.portable_shared_smoke PRIVATE STLAB_UNIT_TEST)
  target_link_libraries(stlab.test.portable_shared_smoke PUBLIC stlab::testing)

  add_test(
    NAME stlab.test.portable_shared_smoke
    COMMAND stlab.test.portable_shared_smoke
  )
endif()

################################################################################
#
# Main executor scenarios. `main_executor_run()` never returns, so each scenario is a standalone
# executable that exits from a task. They link only against `stlab-core` to prove a consumer can
# use the main executor through the exported C ABI alone.
#
if(NOT STLAB_MAIN_EXECUTOR STREQUAL "none")
  set(stlab_main_executor_tests order concurrent shutdown_order)
  if(STLAB_MAIN_EXECUTOR STREQUAL "portable")
    list(APPEND stlab_main_executor_tests pre_exit pre_exit_before_submit)
  endif()

  foreach(scenario IN LISTS stlab_main_executor_tests)
    set(target stlab.test.main_executor_${scenario})
    add_executable(${target} main_executor_${scenario}_test.cpp main_executor_test_host.hpp)
    target_link_libraries(${target} PRIVATE stlab-core)
    if(STLAB_MAIN_EXECUTOR STREQUAL "qt5")
      target_link_libraries(${target} PRIVATE Qt5::Core)
    elseif(STLAB_MAIN_EXECUTOR STREQUAL "qt6")
      target_link_libraries(${target} PRIVATE Qt6::Core)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 60)
    stlab_copy_runtime_dlls(${target})
  endforeach()
endif()

################################################################################

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

target_compile_definitions(stlab.test.future PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.future PUBLIC stlab::testing)

add_test(
  NAME stlab.test.future
  COMMAND stlab.test.future
)


################################################################################

add_executable(stlab.test.serial_queue
  serial_queue_test.cpp
  main.cpp)

target_compile_definitions(stlab.test.serial_queue PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.serial_queue PUBLIC stlab::testing)

add_test(
  NAME stlab.test.serial_queue
  COMMAND stlab.test.serial_queue
)
################################################################################

add_executable(stlab.test.system_timer
  system_timer_test.cpp
  main.cpp)

target_compile_definitions(stlab.test.system_timer PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.system_timer PUBLIC stlab::testing)

add_test(
  NAME stlab.test.system_timer
  COMMAND stlab.test.system_timer
)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  add_executable(stlab.test.executor_priority_shutdown executor_priority_shutdown.cpp)
  target_link_libraries(stlab.test.executor_priority_shutdown PRIVATE stlab-core)
  add_test(NAME stlab.test.executor_priority_shutdown COMMAND stlab.test.executor_priority_shutdown)
  set_tests_properties(stlab.test.executor_priority_shutdown PROPERTIES TIMEOUT 10)
  stlab_copy_runtime_dlls(stlab.test.executor_priority_shutdown)

  add_executable(stlab.test.core_shutdown_first core_shutdown_first.cpp)
  target_link_libraries(stlab.test.core_shutdown_first PRIVATE stlab-core)
  add_test(NAME stlab.test.core_shutdown_first COMMAND stlab.test.core_shutdown_first)
  set_tests_properties(stlab.test.core_shutdown_first PROPERTIES TIMEOUT 60)
  stlab_copy_runtime_dlls(stlab.test.core_shutdown_first)

  set(stlab_timer_scenarios lifecycle shutdown_first executor_shutdown application_shutdown)
  if(WIN32 AND NOT STLAB_CORE_SHARED)
    list(APPEND stlab_timer_scenarios resource_failure)
  endif()
  foreach(scenario IN LISTS stlab_timer_scenarios)
    set(target stlab.test.system_timer_${scenario})
    add_executable(${target} system_timer_${scenario}.cpp)
    target_link_libraries(${target} PRIVATE stlab-core)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 10)
    stlab_copy_runtime_dlls(${target})
  endforeach()
endif()
################################################################################

add_executable(stlab.test.tuple_algorithm
  tuple_algorithm_test.cpp
  main.cpp)

target_compile_definitions(stlab.test.tuple_algorithm PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.tuple_algorithm PUBLIC stlab::testing)

add_test(
  NAME stlab.test.tuple_algorithm
  COMMAND stlab.test.tuple_algorithm
)

################################################################################

add_executable(stlab.test.cow
  cow_test.cpp
  main.cpp)

# Disable warning on GCC 11 and above for cow_test.cpp. See
# github.com/stlab/stlab/issues/438 for details.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "11")
  set_source_files_properties(cow_test.cpp PROPERTIES COMPILE_FLAGS -Wno-free-nonheap-object)
endif()

target_compile_definitions(stlab.test.cow PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.cow PUBLIC stlab::testing)

add_test(
  NAME stlab.test.cow
  COMMAND stlab.test.cow
)

################################################################################

add_executable(stlab.test.task
  task_test.cpp
  main.cpp)

target_compile_definitions(stlab.test.task PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.task PUBLIC stlab::testing)

add_test(
  NAME stlab.test.task
  COMMAND stlab.test.task
)

################################################################################

add_executable(stlab.test.task_abi_mismatch EXCLUDE_FROM_ALL
  task_abi_mismatch_test.cpp)

target_link_libraries(stlab.test.task_abi_mismatch PRIVATE stlab-core)

add_test(
  NAME stlab.test.task_abi_mismatch_link
  COMMAND ${CMAKE_COMMAND}
    -Dbuild_dir=${PROJECT_BINARY_DIR}
    -Dconfig=$<CONFIG>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_task_abi_mismatch.cmake)

################################################################################

add_executable(stlab.test.tuple
  tuple_test.cpp
  main.cpp)

target_compile_definitions(stlab.test.tuple PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.tuple PUBLIC stlab::testing)

add_test(
  NAME stlab.test.tuple
  COMMAND stlab.test.tuple
)

################################################################################

add_executable(stlab.test.traits
  traits_test.cpp
  main.cpp)

target_compile_definitions(stlab.test.traits PRIVATE STLAB_UNIT_TEST)

target_link_libraries(stlab.test.traits PUBLIC stlab::testing)

add_test(
  NAME stlab.test.traits
  COMMAND stlab.test.traits
)

################################################################################

add_executable(stlab.test.forest
  forest_test.cpp
  main.cpp)

target_link_libraries(stlab.test.forest PUBLIC stlab::testing)

add_test(
  NAME stlab.test.forest
  COMMAND stlab.test.forest
)

################################################################################

add_executable(stlab.test.utility
  utility_test.cpp
  main.cpp)

target_link_libraries(stlab.test.utility PUBLIC stlab::testing)

add_test(
  NAME stlab.test.utility
  COMMAND stlab.test.utility
)

################################################################################
#
# tests are compiled without compiler extensions to ensure the stlab headers
# are not dependent upon any such extension.
#
set_target_properties(
  stlab.test.channel
  stlab.test.future
  stlab.test.serial_queue
  stlab.test.cow
  stlab.test.task
  stlab.test.tuple
  stlab.test.traits
  PROPERTIES CXX_EXTENSIONS OFF)

#
# Many of the stlab tests are executed using the system executor which defaults
# to a number of threads equal to the system parallelism.
#
# Here we (attempt to) query the system processor count and store the value in
# the `nProcessors` variable, where a non-zeros value indicates success.
#
# Provided the query was successful, we inform ctest of the test parallism.
#

include(ProcessorCount)
ProcessorCount(nProcessors)

if(nProcessors)
  set_tests_properties(
    stlab.test.channel
    stlab.test.executor
    stlab.test.future
    stlab.test.serial_queue
    stlab.test.cow
    stlab.test.task
    stlab.test.tuple
    PROPERTIES PROCESSORS ${nProcessors})
endif()

foreach(stlab_test_target IN ITEMS
  stlab.test.channel
  stlab.test.executor
  stlab.test.future
  stlab.test.serial_queue
  stlab.test.system_timer
  stlab.test.tuple_algorithm
  stlab.test.cow
  stlab.test.task
  stlab.test.tuple
  stlab.test.traits
  stlab.test.forest
  stlab.test.utility)
  stlab_copy_runtime_dlls(${stlab_test_target})
endforeach()

if(TARGET stlab.test.task_abi_mismatch)
  stlab_copy_runtime_dlls(stlab.test.task_abi_mismatch)
endif()

if(TARGET stlab.test.core_shared_smoke)
  stlab_copy_runtime_dlls(stlab.test.core_shared_smoke)
endif()

if(TARGET stlab.test.portable_shared_smoke)
  stlab_copy_runtime_dlls(stlab.test.portable_shared_smoke)
endif()
