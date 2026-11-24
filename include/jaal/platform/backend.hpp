#pragma once
#include <jaal/meta/target.hpp>
// jaal::platform::backend — WHICH jaal backend runs on this OS. The one
// mapping from the OS (meta/target.hpp) to an implementation, read by:
//
//   platform/select.hpp       the C++ types: native_reactor = epoll_reactor ...
//   CMakeLists.txt            which src/platform/ files to compile, asked of
//                             THIS header (cmake/JaalTarget.cmake), never
//                             re-derived from WIN32 / APPLE / CMAKE_SYSTEM_NAME
//
//   Linux, Android        posix  + epoll
//   macOS, Free/Net/OpenBSD posix + kqueue
//   other __unix__        posix  + poll
//   Windows               windows (wait_reactor)
//
// Each macro is always defined, 0 or 1; exactly one family and one reactor
// is 1.

#if JAAL_OS_WINDOWS
#  define JAAL_PLATFORM_WINDOWS 1
#  define JAAL_PLATFORM_POSIX   0
#else
#  define JAAL_PLATFORM_WINDOWS 0
#  define JAAL_PLATFORM_POSIX   1
#endif

#define JAAL_PLATFORM_EPOLL  (JAAL_PLATFORM_POSIX && JAAL_OS_LINUX)
#define JAAL_PLATFORM_KQUEUE (JAAL_PLATFORM_POSIX && !JAAL_OS_LINUX && \
                              (JAAL_OS_APPLE || JAAL_OS_FREEBSD || JAAL_OS_NETBSD || JAAL_OS_OPENBSD))
#define JAAL_PLATFORM_POLL   (JAAL_PLATFORM_POSIX && !JAAL_PLATFORM_EPOLL && !JAAL_PLATFORM_KQUEUE && JAAL_OS_UNIX)
#define JAAL_PLATFORM_WAIT   JAAL_PLATFORM_WINDOWS

static_assert(JAAL_PLATFORM_WINDOWS + JAAL_PLATFORM_POSIX == 1, "jaal: exactly one platform family");
static_assert(JAAL_PLATFORM_EPOLL + JAAL_PLATFORM_KQUEUE + JAAL_PLATFORM_POLL + JAAL_PLATFORM_WAIT == 1,
              "jaal: no platform backend for this OS");
