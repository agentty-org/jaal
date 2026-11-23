# one_os.cmake — the stack reads the OS from ONE file.
#
#   cmake -DROOTS="<dir>;<dir>;..." -DSOURCE=<the one file> -P one_os.cmake
#
# jaal/meta/target.hpp is the only file in jaal, mayag and hand that
# tests the compiler's OS macros (_WIN32, __APPLE__, __linux__, __unix__,
# __ANDROID__, __FreeBSD__, __OpenBSD__, __NetBSD__, __EMSCRIPTEN__,
# TARGET_OS_*). Everything else tests JAAL_OS_* / MAYAG_OS_*, which are always
# defined (0/1), so a missing include is a -Wundef warning and never a silent
# "false". WHERE a project may branch on the OS at all is its own seam rule
# (jaal: platform/, mayag: one_look, hand: one_hand); this rule is that there
# is one definition of what the OS is.

set(raw "_WIN32|_WIN64|__APPLE__|__MACH__|__linux__|__unix__|__unix[^_]|__ANDROID__|__FreeBSD__|__OpenBSD__|__NetBSD__|__EMSCRIPTEN__|__CYGWIN__|TARGET_OS_")
# CMake regexes have no \b: a raw name must not follow an identifier character
# (MAYAG_WITH_WIN32 contains _WIN32 and is not it)
set(raw "(^|[^A-Za-z0-9_])(${raw})")
set(bad 0)
foreach(root ${ROOTS})
    file(GLOB_RECURSE fs ${root}/*.hpp ${root}/*.cpp ${root}/*.h ${root}/*.mm)
    foreach(f ${fs})
        if(f STREQUAL SOURCE)
            continue()
        endif()
        file(STRINGS ${f} hits REGEX "^[ \t]*#[ \t]*(if|elif|ifdef|ifndef)[ \t(!].*${raw}")
        if(hits)
            message(SEND_ERROR "one_os: ${f} tests a raw OS macro; use JAAL_OS_* / MAYAG_OS_* (jaal/meta/target.hpp): ${hits}")
            math(EXPR bad "${bad}+1")
        endif()
        # *_OS_* undefined would read as 0 -- "not this OS" -- silently. A file
        # that tests them includes the header that defines them, itself.
        file(STRINGS ${f} uses REGEX "^[ \t]*#[ \t]*(if|elif)[ \t(!].*(JAAL|MAYAG)_OS_")
        if(uses)
            file(STRINGS ${f} inc REGEX "#[ \t]*include[ \t]*[<\"](jaal/meta/target\\.hpp|jaal/platform/target\\.hpp|mayag/core/target\\.hpp|[./]*core/target\\.hpp|[./]*platform/target\\.hpp|target\\.hpp)[>\"]")
            if(NOT inc)
                message(SEND_ERROR "one_os: ${f} tests *_OS_* without including target.hpp (it would read as 0): ${uses}")
                math(EXPR bad "${bad}+1")
            endif()
        endif()
    endforeach()
endforeach()
if(bad GREATER 0)
    message(FATAL_ERROR "one_os: ${bad} file(s)")
endif()
message(STATUS "one_os: the OS is read in one file (${SOURCE})")
