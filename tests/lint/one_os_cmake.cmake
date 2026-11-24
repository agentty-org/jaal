# one_os_cmake.cmake — the build asks the platform from ONE place too.
#
#   cmake -DFILES="<CMakeLists.txt>;..." -DALLOW="<file>:<count>;..." -P one_os_cmake.cmake
#
# The C++ decides what the platform is (jaal/meta/target.hpp, and from it
# jaal/platform/backend.hpp and mayag/core/target.hpp). A build script that
# branched on WIN32 / APPLE / UNIX / CMAKE_SYSTEM_NAME would be a second
# copy of that decision, free to drift from it (jaal's source list compiled
# kqueue for every "*BSD" while its header chose poll on one of them). So
# an if() on those is an error, except where ALLOW grants a file an exact
# count: the bootstrap that has no compiler to ask yet, or nothing.
# Everything else asks the headers (cmake/JaalTarget.cmake: jaal_target_is)
# or reads what was asked (MAYAG_SYSTEM, JAAL_PLATFORM_*).
#
# Toolchain questions (MSVC, CMAKE_CXX_COMPILER_ID: which flags this
# compiler takes) are a different axis and stay allowed.

set(raw "(^|[^A-Za-z0-9_\\$\\{])(WIN32|APPLE|UNIX|CYGWIN|MINGW|ANDROID|IOS|LINUX|BSD|CMAKE_SYSTEM_NAME|CMAKE_HOST_SYSTEM_NAME|CMAKE_HOST_WIN32|CMAKE_HOST_APPLE|CMAKE_HOST_UNIX)([^A-Za-z0-9_]|$)")
set(granted "")
foreach(a ${ALLOW})
    string(REGEX MATCH "^(.+):([0-9]+)$" _ "${a}")
    set(allow_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
set(bad 0)
foreach(f ${FILES})
    file(STRINGS ${f} lines REGEX "^[ \t]*(if|elseif)[ \t]*\\(")
    set(n 0)
    set(found "")
    foreach(l ${lines})
        string(REGEX REPLACE "#.*" "" l "${l}")
        if(l MATCHES "${raw}")
            math(EXPR n "${n}+1")
            list(APPEND found "${l}")
        endif()
    endforeach()
    set(want 0)
    if(DEFINED allow_${f})
        set(want ${allow_${f}})
    endif()
    if(NOT n EQUAL want)
        message(SEND_ERROR "one_os_cmake: ${f} branches on the OS ${n} time(s), ${want} granted; ask the headers (jaal_target_is) or read MAYAG_SYSTEM / JAAL_PLATFORM_*:\n${found}")
        math(EXPR bad "${bad}+1")
    endif()
endforeach()
if(bad GREATER 0)
    message(FATAL_ERROR "one_os_cmake: ${bad} file(s)")
endif()
message(STATUS "one_os_cmake: the build asks the platform of the C++ headers")
