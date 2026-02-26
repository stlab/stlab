include(stlab/development)

# test dependencies: doctest is fetched in root CMakeLists when BUILD_TESTING is ON
# Sanitizer options from development.cmake apply to test targets; stlab target gets them in src/CMakeLists.txt.

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
