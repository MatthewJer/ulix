// __config_site of libc++ for Ulix: what CMake would write from libcxx/include/__config_site.in. One line per feature; the comments say why.
#ifndef _LIBCPP___CONFIG_SITE
#define _LIBCPP___CONFIG_SITE

#define _LIBCPP_ABI_VERSION 1
#define _LIBCPP_ABI_NAMESPACE __1
#define _LIBCPP_ABI_FORCE_ITANIUM 0
#define _LIBCPP_ABI_FORCE_MICROSOFT 0
#define _LIBCPP_HAS_THREADS 0                    // no std::thread and no locking yet (first the futex work, issue #36)
#define _LIBCPP_HAS_MONOTONIC_CLOCK 1            // clock_gettime (CLOCK_MONOTONIC) of the kernel
#define _LIBCPP_HAS_TERMINAL 0
#define _LIBCPP_HAS_MUSL_LIBC 1
#define _LIBCPP_HAS_THREAD_API_PTHREAD 0
#define _LIBCPP_HAS_THREAD_API_EXTERNAL 0
#define _LIBCPP_HAS_THREAD_API_WIN32 0
#define _LIBCPP_HAS_THREAD_API_C11 0
#define _LIBCPP_DISABLE_VISIBILITY_ANNOTATIONS   // one static library: every symbol is visible
#define _LIBCPP_HAS_VENDOR_AVAILABILITY_ANNOTATIONS 0
#define _LIBCPP_TYPEINFO_COMPARISON_IMPLEMENTATION 1
#define _LIBCPP_HAS_FILESYSTEM 0                 // <filesystem> needs more of the kernel than there is (directory iteration, permissions)
#define _LIBCPP_HAS_RANDOM_DEVICE 0
#define _LIBCPP_HAS_LOCALIZATION 1               // <iostream>, <locale> (musl has the C and C.UTF-8 locales only)
#define _LIBCPP_HAS_UNICODE 1
#define _LIBCPP_HAS_WIDE_CHARACTERS 1
#define _LIBCPP_HAS_NO_STD_MODULES
#define _LIBCPP_HAS_TIME_ZONE_DATABASE 0
#define _LIBCPP_INSTRUMENTED_WITH_ASAN 0
#define _LIBCPP_PSTL_BACKEND_SERIAL
#define _LIBCPP_HARDENING_MODE_DEFAULT _LIBCPP_HARDENING_MODE_NONE

#endif // _LIBCPP___CONFIG_SITE
