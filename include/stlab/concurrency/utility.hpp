/*
    Copyright 2015 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

/**************************************************************************************************/

#ifndef STLAB_CONCURRENCY_UTILITY_HPP
#define STLAB_CONCURRENCY_UTILITY_HPP

/*! @file utility.hpp
 *  @brief Convenience includes for ready futures and await helpers.
 */

#include <stlab/concurrency/ready_future.hpp>
#include <stlab/concurrency/await.hpp>

/**************************************************************************************************/

#if 0

#include <thread>
#include <functional>
#include <cstdio>

// useful macro for debugging
#define STLAB_TRACE(S)                          \
    std::printf("%s:%d %d %s\n", __FILE__, __LINE__, \
           (int)std::hash<std::thread::id>()(std::this_thread::get_id()), S);

#endif

/**************************************************************************************************/

#endif // STLAB_CONCURRENCY_UTILITY_HPP
