cmake_minimum_required( VERSION 3.2 )
set( CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configuration types" FORCE )

project( stlab_examples LANGUAGES C CXX )

file( GLOB_RECURSE examples RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/libraries "libraries/*.cpp" )
add_subdirectory( stlab )

# [DEPENDENCY] https://www.boost.org/releases/ (minimum supported version, not a pin)
find_package( Boost 1.60.0 REQUIRED )

include_directories(${Boost_INCLUDE_DIRS})

foreach( path IN LISTS examples )
    if (NOT (path MATCHES "operator_co_await_example\.cpp" OR path MATCHES "scope_example_return\.cpp") )
        string( FIND "${path}" "." target REVERSE )
        string( SUBSTRING "${path}" 0 "${target}" target )
        string( REPLACE "/" "." target "${target}" )

        add_executable( ${target} "libraries/${path}" )
        set_property( TARGET ${target} PROPERTY CXX_STANDARD 17 )
        set_property( TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)

        target_link_libraries( ${target} PUBLIC stlab )
        add_test( NAME ${target} COMMAND ${target} )
    endif ()
endforeach()
