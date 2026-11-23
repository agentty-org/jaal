#pragma once
// jaal::platform::target — the operating system this is compiled for.
//
// In meta/ because it depends on nothing: every layer (meta, core, kernel,
// platform) and every project above may include it.
//
// THE one place in jaal, mayag and hand that reads the compiler's OS macros
// (_WIN32, __APPLE__, __linux__, ...). Everything else asks here:
//
//   #if JAAL_OS_WINDOWS                    a preprocessor decision (an OS header,
//                                          code that only compiles there)
//   if constexpr (platform::os == os_kind::windows)   everything that compiles
//                                          everywhere: prefer it, both branches
//                                          are type-checked on every build
//
// mayag re-exports these as MAYAG_OS_* (core/target.hpp) so hand never names
// jaal. The lint tests/lint/one_os.cmake fails any tree in the chain that
// tests a raw OS macro outside this file, and where it is allowed to test
// these is each project's own seam rule (jaal: platform/; mayag: one_look;
// hand: one_hand). Every macro is ALWAYS defined, 0 or 1: a misspelt or
// forgotten include is a -Wundef warning, never a silent "false".

// ── the facts, read once ─────────────────────────────────────────────────────
#if defined(_WIN32)
#  define JAAL_OS_WINDOWS 1
#else
#  define JAAL_OS_WINDOWS 0
#endif
#if defined(__APPLE__)
#  define JAAL_OS_APPLE 1
#else
#  define JAAL_OS_APPLE 0
#endif
#if defined(__linux__)
#  define JAAL_OS_LINUX 1
#else
#  define JAAL_OS_LINUX 0
#endif
#if defined(__ANDROID__)
#  define JAAL_OS_ANDROID 1
#else
#  define JAAL_OS_ANDROID 0
#endif
#if defined(__FreeBSD__)
#  define JAAL_OS_FREEBSD 1
#else
#  define JAAL_OS_FREEBSD 0
#endif
#if defined(__OpenBSD__)
#  define JAAL_OS_OPENBSD 1
#else
#  define JAAL_OS_OPENBSD 0
#endif
#if defined(__NetBSD__)
#  define JAAL_OS_NETBSD 1
#else
#  define JAAL_OS_NETBSD 0
#endif
#if defined(__EMSCRIPTEN__)
#  define JAAL_OS_EMSCRIPTEN 1
#else
#  define JAAL_OS_EMSCRIPTEN 0
#endif
#if defined(__unix__)
#  define JAAL_OS_UNIX 1   // the compiler's __unix__ (Linux, BSDs; NOT macOS)
#else
#  define JAAL_OS_UNIX 0
#endif

/// POSIX in the sense this stack uses: not Windows.
#define JAAL_OS_POSIX (!JAAL_OS_WINDOWS)
/// The free desktops: Wayland / fontconfig / epoll-or-kqueue land.
#define JAAL_OS_FREEDESKTOP (JAAL_OS_LINUX || JAAL_OS_FREEBSD)

namespace jaal::platform {

enum class os_kind : unsigned char { windows, apple, linux, android, freebsd, openbsd, netbsd, emscripten, other_unix };

/// The OS as a value, for `if constexpr` and tables.
inline constexpr os_kind os =
    JAAL_OS_WINDOWS ? os_kind::windows : JAAL_OS_APPLE ? os_kind::apple : JAAL_OS_ANDROID ? os_kind::android
  : JAAL_OS_LINUX ? os_kind::linux : JAAL_OS_FREEBSD ? os_kind::freebsd : JAAL_OS_OPENBSD ? os_kind::openbsd
  : JAAL_OS_NETBSD ? os_kind::netbsd : JAAL_OS_EMSCRIPTEN ? os_kind::emscripten : os_kind::other_unix;

static_assert(JAAL_OS_WINDOWS + JAAL_OS_APPLE + JAAL_OS_POSIX >= 1, "jaal: an OS it cannot name");

}  // namespace jaal::platform
