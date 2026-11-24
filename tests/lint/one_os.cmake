# one_os.cmake — the stack reads its platform from ONE file each.
#
#   cmake -DROOTS="<dir>;..." -DSOURCE="<reader>[;<reader>]" [-DBUILD_SOURCE=<file>] -P one_os.cmake
#
# SOURCE is one reader per STANDALONE project: jaal/meta/target.hpp for
# jaal, mayag and hand; toe/core/target.hpp for toe, which depends on
# nothing (hand static_asserts the two agree).
#
# SOURCE (jaal/meta/target.hpp) is the only file in jaal, mayag and hand
# that tests the compiler's predefined TARGET macros:
#   the OS        _WIN32, __APPLE__, __linux__, __unix__, __ANDROID__, *BSD__,
#                 __EMSCRIPTEN__, TARGET_OS_*
#   the compiler  _MSC_VER, __clang__, __GNUC__, __apple_build_version__
#   the CPU / ISA __x86_64__, __aarch64__, _M_X64, _M_ARM64, _M_IX86*,
#                 __SSE*__, __AVX*__, __ARM_NEON*
# Everything else tests JAAL_{OS,CC,CPU,ISA}_* / MAYAG_{OS,CC,CPU,ISA}_*.
#
# BUILD_SOURCE (mayag/core/target.hpp), when given, is the only file that
# tests the BUILD's platform choices -- MAYAG_WITH_* (CMake's renderer and
# window-system options) and MAYAG_PLATFORM_HEADLESS -- and turns them into
# MAYAG_GPU_* and the one window-system answer, MAYAG_WINDOW_*.
#
# Every derived name is always defined (0/1), so a file that tests one must
# include its header itself: an undefined one would read as 0, "not this
# platform", silently. WHERE a project may branch on the platform at all is
# its own seam rule (jaal: platform/, mayag: one_look, hand: one_hand); this
# rule is that there is one definition of what the platform is.

cmake_policy(SET CMP0057 NEW)   # if(IN_LIST), off by default in -P scripts
# A project above jaal that re-exports the facts under its own prefix (maya:
# MAYA_OS_* from maya/core/target.hpp) passes DERIVED="JAAL|MAYAG|MAYA" and
# HEADERS=<regex of the paths that define them>, so the same rule holds there.
if(NOT DEFINED DERIVED)
    set(DERIVED "JAAL|MAYAG")
endif()
if(NOT DEFINED HEADERS)
    set(HEADERS "jaal/meta/target\\.hpp|jaal/platform/target\\.hpp|mayag/core/target\\.hpp|[./]*core/target\\.hpp|[./]*platform/target\\.hpp|target\\.hpp")
endif()
set(raw "_WIN32|_WIN64|__APPLE__|__MACH__|__linux__|__unix__|__unix[^_]|__ANDROID__|__FreeBSD__|__OpenBSD__|__NetBSD__|__EMSCRIPTEN__|__CYGWIN__|TARGET_OS_")
string(APPEND raw "|_MSC_VER|_MSC_FULL_VER|__clang__|__GNUC__|__apple_build_version__")
string(APPEND raw "|__x86_64__|__amd64__|__aarch64__|__arm64__|__i386__|_M_X64|_M_AMD64|_M_ARM64|_M_IX86|__SSE|__AVX|__ARM_NEON")
# CMake regexes have no \b: a raw name must not follow an identifier character
# (MAYAG_WITH_WIN32 contains _WIN32 and is not it)
set(raw "(^|[^A-Za-z0-9_])(${raw})")
set(build_raw "(^|[^A-Za-z0-9_])(MAYAG_WITH_[A-Z0-9_]+|MAYAG_PLATFORM_HEADLESS)")
set(bad 0)
foreach(root ${ROOTS})
    file(GLOB_RECURSE fs ${root}/*.hpp ${root}/*.cpp ${root}/*.h ${root}/*.mm)
    foreach(f ${fs})
        if(f IN_LIST SOURCE)
            continue()
        endif()
        file(STRINGS ${f} hits REGEX "^[ \t]*#[ \t]*(if|elif|ifdef|ifndef)[ \t(!].*${raw}")
        if(hits)
            message(SEND_ERROR "one_os: ${f} tests a raw target macro; use JAAL_/MAYAG_ OS_/CC_/CPU_/ISA_* (jaal/meta/target.hpp): ${hits}")
            math(EXPR bad "${bad}+1")
        endif()
        if(BUILD_SOURCE AND NOT f STREQUAL BUILD_SOURCE)
            file(STRINGS ${f} hits REGEX "^[ \t]*#[ \t]*(if|elif|ifdef|ifndef)[ \t(!].*${build_raw}")
            if(hits)
                message(SEND_ERROR "one_os: ${f} tests a build option directly; use MAYAG_GPU_* / MAYAG_WINDOW_* (mayag/core/target.hpp): ${hits}")
                math(EXPR bad "${bad}+1")
            endif()
        endif()
        # a derived name undefined would read as 0 -- "not this platform" --
        # silently. A file that tests one includes the header that defines it.
        file(STRINGS ${f} uses REGEX "^[ \t]*#[ \t]*(if|elif)[ \t(!].*(${DERIVED})_(OS|CC|CPU|ISA|GPU|WINDOW)_")
        if(uses)
            file(STRINGS ${f} inc REGEX "#[ \t]*include[ \t]*[<\"](${HEADERS})[>\"]")
            if(NOT inc)
                message(SEND_ERROR "one_os: ${f} tests a platform fact without including target.hpp (it would read as 0): ${uses}")
                math(EXPR bad "${bad}+1")
            endif()
        endif()
    endforeach()
endforeach()
if(bad GREATER 0)
    message(FATAL_ERROR "one_os: ${bad} file(s)")
endif()
message(STATUS "one_os: the target is read in one file (${SOURCE}); the build's choices in one (${BUILD_SOURCE})")
