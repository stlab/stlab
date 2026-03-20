#ifndef STLAB_SET_CURRENT_THREAD_NAME_HPP
#define STLAB_SET_CURRENT_THREAD_NAME_HPP

/*! @file set_current_thread_name.hpp
 *  @brief Set the current thread name (platform-specific).
 */

#include <stlab/config.hpp>

#if STLAB_THREADS(WIN32)
  #include <cstring>
  #include <string>

  #include <windows.h>

  #include <processthreadsapi.h>
  #include <stringapiset.h>
#elif STLAB_THREADS(PTHREAD)
  #include <pthread.h>
#elif STLAB_THREADS(PTHREAD_EMSCRIPTEN)
  #include <pthread.h>
  #include <emscripten/threading.h>
#elif STLAB_THREADS(PTHREAD_APPLE)
  #include <pthread.h>
#elif STLAB_THREADS(NONE)
  // Do nothing
#else
  #error "Unspecified or unknown thread mode set."
#endif

/** @defgroup stlab_concurrency_set_current_thread_name set_current_thread_name
 *  @ingroup stlab_concurrency
 *  @brief Set the current thread name (platform-specific).
 *  @{
 */

namespace stlab {

inline void set_current_thread_name(const char* name) {
#if STLAB_THREADS(WIN32)
    /* Should string->wstring be split out to a utility? */
    int count = MultiByteToWideChar(CP_UTF8, 0, name, static_cast<int>(std::strlen(name)), NULL, 0);
    if (count <= 0) return;
    std::wstring str(count, wchar_t{});
    count = MultiByteToWideChar(CP_UTF8, 0, name, static_cast<int>(std::strlen(name)), &str[0],
                                static_cast<int>(str.size()));
    if (count <= 0) return;

    (void)SetThreadDescription(GetCurrentThread(), str.c_str());
#elif STLAB_THREADS(PTHREAD_EMSCRIPTEN)
    emscripten_set_thread_name(pthread_self(), name);
#elif STLAB_THREADS(PTHREAD_APPLE)
    pthread_setname_np(name);
#elif STLAB_THREADS(PTHREAD)
    pthread_setname_np(pthread_self(), name);
#elif STLAB_THREADS(NONE)
    // Nothing
#else
    #error "Unspecified or unknown thread mode set."
#endif
}

} // namespace stlab

/** @} */

#endif
