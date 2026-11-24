#pragma once
// jaal::platform::target — what this is compiled FOR: the operating system,
// the compiler, the CPU and the vector instructions it may use.
//
// In meta/ because it depends on nothing: every layer (meta, core, kernel,
// platform) and every project above may include it.
//
// THE one place in jaal, mayag and hand that reads the compiler's
// predefined target macros (_WIN32, __APPLE__, __linux__, _MSC_VER,
// __clang__, __GNUC__, __aarch64__, __SSE2__, ...). Everything else asks here:
//
//   #if JAAL_OS_WINDOWS                    a preprocessor decision (an OS header,
//                                          code that only compiles there)
//   if constexpr (platform::os == os_kind::windows)   everything that compiles
//                                          everywhere: prefer it, both branches
//                                          are type-checked on every build
//
// mayag re-exports these as MAYAG_OS_* / MAYAG_CC_* / MAYAG_CPU_* /
// MAYAG_ISA_* (core/target.hpp) so hand never names jaal. The lint
// tests/lint/one_os.cmake fails any tree in the chain that tests a raw
// target macro outside this file, or tests one of these without including
// it; where it is allowed to test the OS is each project's own seam rule
// (jaal: platform/; mayag: one_look; hand: one_hand). Every macro is ALWAYS
// defined, 0 or 1, so `#if` (never `#ifdef`) reads it.

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

// ── the compiler ─────────────────────────────────────────────────────────────────────────────
// Exactly one of CLANG / GCC / MSVC is 1 on a compiler this stack builds
// with. clang-cl is CLANG (with the MSVC ABI); Apple's clang is CLANG and
// also APPLE_CLANG (its own version numbers and its own bugs).
#if defined(__clang__)
#  define JAAL_CC_CLANG 1
#else
#  define JAAL_CC_CLANG 0
#endif
#if defined(__GNUC__) && !defined(__clang__)
#  define JAAL_CC_GCC 1
#else
#  define JAAL_CC_GCC 0
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#  define JAAL_CC_MSVC 1
#else
#  define JAAL_CC_MSVC 0
#endif
#if defined(_MSC_VER)
#  define JAAL_CC_MSVC_ABI 1   // cl or clang-cl: MSVC's ABI and library
#else
#  define JAAL_CC_MSVC_ABI 0
#endif
#if defined(__apple_build_version__)
#  define JAAL_CC_APPLE_CLANG 1
#else
#  define JAAL_CC_APPLE_CLANG 0
#endif
/// GNU extensions (__PRETTY_FUNCTION__, __is_same): GCC and every clang,
/// clang-cl included.
#define JAAL_CC_GNU_EXT (JAAL_CC_CLANG || JAAL_CC_GCC)
/// A native 128-bit integer (__int128): GCC and clang on 64-bit targets,
/// never MSVC. Code that wants one tests this and keeps an exact fallback.
#if defined(__SIZEOF_INT128__)
#  define JAAL_CC_INT128 1
#else
#  define JAAL_CC_INT128 0
#endif

// ── the CPU and its vector unit ─────────────────────────────────────────────────────────────
#if defined(__aarch64__) || defined(_M_ARM64)
#  define JAAL_CPU_ARM64 1
#else
#  define JAAL_CPU_ARM64 0
#endif
#if defined(__x86_64__) || defined(_M_X64)
#  define JAAL_CPU_X86_64 1
#else
#  define JAAL_CPU_X86_64 0
#endif
/// 32-bit x86 (i386..i686, MSVC's x86): a release target of its own for
/// some projects above (their asset names, their syscall tables).
#if defined(__i386__) || defined(_M_IX86)
#  define JAAL_CPU_X86_32 1
#else
#  define JAAL_CPU_X86_32 0
#endif
/// What the compiler may emit for this build (its -m flags), not what the
/// machine it runs on has: SIMD code tests these, never the raw __SSE2__.
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#  define JAAL_ISA_NEON 1
#else
#  define JAAL_ISA_NEON 0
#endif
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#  define JAAL_ISA_SSE2 1
#else
#  define JAAL_ISA_SSE2 0
#endif
#if defined(__SSE4_1__)
#  define JAAL_ISA_SSE41 1
#else
#  define JAAL_ISA_SSE41 0
#endif
#if defined(__SSE4_2__)
#  define JAAL_ISA_SSE42 1
#else
#  define JAAL_ISA_SSE42 0
#endif
#if defined(__AVX2__)
#  define JAAL_ISA_AVX2 1
#else
#  define JAAL_ISA_AVX2 0
#endif
#if defined(__AVX512F__)
#  define JAAL_ISA_AVX512F 1
#else
#  define JAAL_ISA_AVX512F 0
#endif

namespace jaal::platform {

// `gnu_linux`, not `linux`: in the GNU dialects (-std=gnu++NN, GCC's and
// clang's DEFAULT) `linux` and `unix` are predefined macros, = 1. An
// enumerator named `linux` made this header a syntax error there -- which
// is how CMake's checks against it (jaal_target_is) all read "no" on Linux.
enum class os_kind : unsigned char { windows, apple, gnu_linux, android, freebsd, openbsd, netbsd, emscripten, other_unix };

/// The OS as a value, for `if constexpr` and tables.
inline constexpr os_kind os =
    JAAL_OS_WINDOWS ? os_kind::windows : JAAL_OS_APPLE ? os_kind::apple : JAAL_OS_ANDROID ? os_kind::android
  : JAAL_OS_LINUX ? os_kind::gnu_linux : JAAL_OS_FREEBSD ? os_kind::freebsd : JAAL_OS_OPENBSD ? os_kind::openbsd
  : JAAL_OS_NETBSD ? os_kind::netbsd : JAAL_OS_EMSCRIPTEN ? os_kind::emscripten : os_kind::other_unix;

static_assert(JAAL_OS_WINDOWS + JAAL_OS_APPLE + JAAL_OS_POSIX >= 1, "jaal: an OS it cannot name");

}  // namespace jaal::platform
