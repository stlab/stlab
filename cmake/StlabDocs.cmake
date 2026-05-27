# SPDX-License-Identifier: BSL-1.0
# stlab documentation setup: merge cpp-library Doxyfile baseline + stlab overlay, then docs target.

include_guard(GLOBAL)

# Merges docs/Doxyfile.in and docs/Doxyfile.stlab into DOXYFILE_OUT (both @ONLY configured first).
function(_stlab_write_merged_doxyfile DOXYFILE_OUT)
  set(base_in "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
  set(stlab_in "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.stlab")
  if(NOT EXISTS "${base_in}" OR NOT EXISTS "${stlab_in}")
    message(FATAL_ERROR "stlab: missing docs/Doxyfile.in or docs/Doxyfile.stlab")
  endif()

  set(base_cfg "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.in.cfg")
  set(stlab_cfg "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.stlab.cfg")
  configure_file("${base_in}" "${base_cfg}" @ONLY)
  configure_file("${stlab_in}" "${stlab_cfg}" @ONLY)

  file(READ "${base_cfg}" base_text)
  file(READ "${stlab_cfg}" stlab_text)
  file(WRITE "${DOXYFILE_OUT}" "${base_text}\n${stlab_text}")
endfunction()

# Same contract as cpp-library _cpp_library_setup_docs(), using merged Doxyfile.
function(stlab_setup_docs)
  set(one_value_args NAME VERSION DESCRIPTION)
  set(multi_value_args DOCS_EXCLUDE_SYMBOLS)
  cmake_parse_arguments(ARG "" "${one_value_args}" "${multi_value_args}" ${ARGN})

  if(NOT ARG_NAME OR NOT ARG_VERSION OR NOT ARG_DESCRIPTION)
    message(FATAL_ERROR "stlab_setup_docs: NAME, VERSION, and DESCRIPTION are required")
  endif()

  find_package(Doxygen)
  if(NOT DOXYGEN_FOUND)
    message(WARNING "Doxygen not found. Documentation will not be built.")
    return()
  endif()

  # [DEPENDENCY] https://github.com/jothepro/doxygen-awesome-css/releases
  CPMAddPackage(
    URI gh:jothepro/doxygen-awesome-css@2.4.2
    DOWNLOAD_ONLY YES
  )

  set(AWESOME_CSS_DIR ${doxygen-awesome-css_SOURCE_DIR})
  set(DOXYFILE_OUT "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile")

  set(PROJECT_NAME "${ARG_NAME}")
  set(PROJECT_BRIEF "${ARG_DESCRIPTION}")
  set(PROJECT_VERSION "${ARG_VERSION}")
  set(INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
  set(OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}")
  set(AWESOME_CSS_PATH "${AWESOME_CSS_DIR}")
  set(EXAMPLES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/examples")
  set(STLAB_DOXYGEN_EXTRA_INPUT "${CMAKE_CURRENT_SOURCE_DIR}/docs/doxygen")
  set(CPP_LIBRARY_ROOT "${cpp-library_SOURCE_DIR}")

  if(ARG_DOCS_EXCLUDE_SYMBOLS)
    string(REPLACE ";" " " EXCLUDE_SYMBOLS "${ARG_DOCS_EXCLUDE_SYMBOLS}")
  else()
    set(EXCLUDE_SYMBOLS "")
  endif()

  if(STLAB_DOXYGEN_RELAXED_WARNINGS)
    set(STLAB_DOXYGEN_WARN_IF_UNDOCUMENTED "NO")
    set(STLAB_DOXYGEN_WARN_AS_ERROR "NO")
  else()
    set(STLAB_DOXYGEN_WARN_IF_UNDOCUMENTED "YES")
    set(STLAB_DOXYGEN_WARN_AS_ERROR "YES")
  endif()

  _stlab_write_merged_doxyfile("${DOXYFILE_OUT}")

  # VERBATIM: do not use shell redirection (e.g. 2>&1); it is passed as a literal extra argument.
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )

  file(MAKE_DIRECTORY ${OUTPUT_DIR})
  message(STATUS "stlab: documentation target 'docs' configured (merged Doxyfile)")
  message(STATUS "stlab: run 'cmake --build . --target docs' to generate documentation")
endfunction()
