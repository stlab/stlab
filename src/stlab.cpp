/*
    Copyright 2026 Adobe
    Distributed under the Boost Software License, Version 1.0.
    (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/

#if defined(_WIN32) && defined(STLAB_BUILD_SHARED_LIBRARY)
extern "C" __declspec(dllexport) int stlab_build_shared_anchor = 0;
#endif
