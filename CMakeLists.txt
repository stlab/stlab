cmake_minimum_required(VERSION 3.24)

include(cmake/CPM.cmake)
# [DEPENDENCY] https://github.com/stlab/cpp-library/releases
CPMAddPackage(
  NAME cpp-library
  GITHUB_REPOSITORY stlab/cpp-library
  GIT_TAG v5.5.0)
include("${cpp-library_SOURCE_DIR}/cpp-library.cmake")
cpp_library_enable_dependency_tracking()

set(CMAKE_CXX_STANDARD 20 CACHE STRING "The C++ standard to use")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
project(stlab LANGUAGES CXX)
cpp_library_set_version()
option(STLAB_INSTALL "Enable installation of stlab" ${PROJECT_IS_TOP_LEVEL})

add_compile_options(
  $<$<CXX_COMPILER_ID:MSVC>:/EHsc>
  $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:AppleClang>>:-fexceptions>)

include(CTest)
include(GNUInstallDirs)
include(CMakeDependentOption)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# A development SHA makes CPM infer version 0, which rejects independently
# installed major versions. There is deliberately no execution version floor yet.
if(CPM_USE_LOCAL_PACKAGES AND NOT CPM_stlab-execution_SOURCE)
  find_package(stlab-execution QUIET CONFIG GLOBAL)
  if(stlab-execution_FOUND)
    CPMRegisterPackage(stlab-execution "${stlab-execution_VERSION}")
  endif()
endif()
# Local development pin; release constraints are assigned when execution is published.
# [DEPENDENCY] https://github.com/stlab/stlab-execution/releases (no release yet; retain development SHA)
CPMAddPackage(
  NAME stlab-execution
  GITHUB_REPOSITORY stlab/stlab-execution
  GIT_TAG b71e7fc85e4bd8312e66095cb79cdde2aba2f0ad)
# Backend selection belongs to execution. These resolved values are needed only by private tests.
get_target_property(stlab_execution_imported stlab::execution IMPORTED)
get_target_property(stlab_execution_type stlab::execution TYPE)
if(NOT stlab_execution_imported)
  get_target_property(stlab_execution_source stlab::execution SOURCE_DIR)
  foreach(setting IN ITEMS STLAB_THREAD_SYSTEM STLAB_TASK_SYSTEM STLAB_MAIN_EXECUTOR)
    get_directory_property(${setting} DIRECTORY "${stlab_execution_source}" DEFINITION ${setting})
  endforeach()
endif()

add_library(stlab)
add_library(stlab::stlab ALIAS stlab)
add_library(stlab-core INTERFACE)
add_library(stlab::stlab-core ALIAS stlab-core)
target_link_libraries(stlab PUBLIC
  $<BUILD_INTERFACE:stlab::execution>
  $<INSTALL_INTERFACE:stlab::$<1:execution>>)
target_link_libraries(stlab-core INTERFACE
  $<BUILD_INTERFACE:stlab::execution>
  $<INSTALL_INTERFACE:stlab::$<1:execution>>)
# Defer the installed package's target lookup to the consumer. Otherwise CMake
# requires the source dependency to be exported even when its install option is OFF.

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)
endif()
cmake_dependent_option(stlab.coverage
  "Enable binary instrumentation to collect test coverage information in the DEBUG configuration"
  OFF PROJECT_IS_TOP_LEVEL OFF)

cmake_dependent_option(STLAB_NO_STD_COROUTINES
  "Suppress usage of standard coroutines. Useful for non-conforming compilers."
  OFF "CMAKE_CXX_STANDARD VERSION_GREATER_EQUAL 20" ON)
if(STLAB_NO_STD_COROUTINES)
  set(STLAB_STD_COROUTINES 0)
else()
  set(STLAB_STD_COROUTINES 1)
endif()

target_include_directories(stlab PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(stlab PUBLIC $<$<CXX_COMPILER_ID:MSVC>:NOMINMAX>)
if(WIN32 AND BUILD_SHARED_LIBS)
  target_compile_definitions(stlab PRIVATE STLAB_BUILD_SHARED_LIBRARY)
endif()

add_subdirectory(include/stlab)
add_subdirectory(src)

# [DEPENDENCY] https://github.com/stlab/stlab-copy-on-write/releases
CPMAddPackage(
  NAME stlab-copy-on-write
  GITHUB_REPOSITORY stlab/stlab-copy-on-write
  VERSION 1.1.1
  GIT_TAG v1.1.1)
target_link_libraries(stlab PUBLIC
  $<BUILD_INTERFACE:stlab::copy-on-write>
  $<INSTALL_INTERFACE:stlab::$<1:copy-on-write>>)
# [DEPENDENCY] https://github.com/stlab/stlab-enum-ops/releases
CPMAddPackage(
  NAME stlab-enum-ops
  GITHUB_REPOSITORY stlab/stlab-enum-ops
  VERSION 1.2.0
  GIT_TAG v1.2.0)
target_link_libraries(stlab PUBLIC
  $<BUILD_INTERFACE:stlab::enum-ops>
  $<INSTALL_INTERFACE:stlab::$<1:enum-ops>>)

message(STATUS "stlab: Disable Coroutines: ${STLAB_NO_STD_COROUTINES}")

if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR STLAB_EMSCRIPTEN_PTHREADS)
    # [DEPENDENCY] https://github.com/doctest/doctest/releases
    CPMAddPackage("gh:doctest/doctest@2.5.3")
  endif()
  add_subdirectory(test)
endif()

# cpp-library convention (see templates/CMakePresets.json "docs" preset).
option(BUILD_DOCS "Generate API documentation with Doxygen (requires Doxygen on PATH)" OFF)
option(STLAB_DOXYGEN "Deprecated alias for BUILD_DOCS" OFF)
if(STLAB_DOXYGEN AND NOT BUILD_DOCS)
  set(BUILD_DOCS ON CACHE BOOL "Generate API documentation with Doxygen" FORCE)
  message(STATUS "stlab: STLAB_DOXYGEN is deprecated; use BUILD_DOCS or cmake --preset=docs")
endif()
# Default ON until Hyde→Doxygen migration reaches full symbol coverage; then OFF to match cpp-library.
option(STLAB_DOXYGEN_RELAXED_WARNINGS
  "Disable WARN_IF_UNDOCUMENTED / WARN_AS_ERROR (set OFF for cpp-library-strict builds)" ON)
if(PROJECT_IS_TOP_LEVEL AND BUILD_DOCS)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/StlabDocs.cmake")
  stlab_setup_docs(
    NAME "stlab"
    VERSION "${PROJECT_VERSION}"
    DESCRIPTION "Modern, modular C++ algorithms, data structures, and concurrency primitives"
    DOCS_EXCLUDE_SYMBOLS "stlab::detail")
endif()

include("${cpp-library_SOURCE_DIR}/cmake/cpp-library-install.cmake")
cpp_library_map_dependency("stlab::execution" "stlab-execution")
# These existing release dependencies can be supplied by CPM's source cache without
# a tracked find_package call; retain their actual package constraints for installation.
cpp_library_map_dependency("stlab::copy-on-write" "stlab-copy-on-write 1.1.1")
cpp_library_map_dependency("stlab::enum-ops" "stlab-enum-ops 1.2.0")
_cpp_library_setup_install(
  NAME "stlab"
  PACKAGE_NAME "stlab"
  VERSION "${PROJECT_VERSION}"
  NAMESPACE "stlab"
  INSTALL_OPTION STLAB_INSTALL
  HEADERS TRUE)
if(STLAB_INSTALL)
  install(TARGETS stlab-core EXPORT stlabTargets)
endif()
