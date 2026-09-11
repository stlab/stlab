/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_EXPORT_HPP
#define STLAB_EXPORT_HPP

/**************************************************************************************************/

#if defined _WIN32
    #define STLAB_EXPORT __declspec(dllexport)
    #define STLAB_IMPORT __declspec(dllimport)
    #define STLAB_HIDDEN
#else
    #define STLAB_EXPORT __attribute__((visibility("default")))
    #define STLAB_IMPORT
    #define STLAB_HIDDEN __attribute__((visibility("hidden")))
#endif

// STLAB_API  - annotates symbols that cross the shared library boundary.
// STLAB_LOCAL - annotates symbols explicitly hidden from the shared library boundary.
//
// Build system requirements:
//   STLAB_STATIC  - define for static library builds
//   STLAB_EXPORTS - define when compiling the stlab shared library itself (dllexport)
//   neither       - when consuming the stlab shared library (dllimport)
#if defined STLAB_STATIC
    #define STLAB_API
    #define STLAB_LOCAL
#elif defined STLAB_EXPORTS
    #define STLAB_API STLAB_EXPORT
    #define STLAB_LOCAL STLAB_HIDDEN
#else
    #define STLAB_API STLAB_IMPORT
    #define STLAB_LOCAL STLAB_HIDDEN
#endif

/**************************************************************************************************/

#endif // STLAB_EXPORT_HPP

/**************************************************************************************************/
