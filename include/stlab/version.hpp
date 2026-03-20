/*
    Copyright 2015-2021 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

//  See http://www.stlab.cc for documentation

#ifndef STLAB_VERSION_HPP
#define STLAB_VERSION_HPP

/*! @file version.hpp
 *  @brief Library version number macros.
 */

#include <stlab/config.hpp>

/** @defgroup stlab_version version
 *  @ingroup stlab
 *  @brief Library version number macros.
 *  @{
 */

//  These version numbers are automatically generated from the CMake project version.

/*!
    `STLAB_VERSION % 100` is the patch level
    `STLAB_VERSION / 100 % 1000` is the minor version
    `STLAB_VERSION / 100000` is the major version
*/
#define STLAB_VERSION STLAB_VERSION_MAJOR() * 100000 + STLAB_VERSION_MINOR() * 100 + STLAB_VERSION_PATCH()

/*!
    `STLAB_LIB_VERSION` is a string in the form "x_y[_z]" where x is the major version
    number, y is the minor version number, and z is the patch level if not 0.
*/

#if STLAB_VERSION_PATCH() == 0
#define STLAB_LIB_VERSION STLAB_STRINGIZE(STLAB_VERSION_MAJOR()) "_" STLAB_STRINGIZE(STLAB_VERSION_MINOR())
#else
#define STLAB_LIB_VERSION STLAB_STRINGIZE(STLAB_VERSION_MAJOR()) "_" STLAB_STRINGIZE(STLAB_VERSION_MINOR()) "_" STLAB_STRINGIZE(STLAB_VERSION_PATCH())
#endif

/** @} */

#endif
