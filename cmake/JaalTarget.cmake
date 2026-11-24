# JaalTarget.cmake — CMake asks the C++ headers what the platform is.
#
# The stack decides its platform in C++, once: jaal/meta/target.hpp (the OS,
# compiler, CPU) and jaal/platform/backend.hpp (which jaal backend runs on
# that OS). A build script that re-derived the same answers from WIN32 /
# APPLE / CMAKE_SYSTEM_NAME would be a second copy, free to drift: a source
# list that compiles kqueue where the header chose poll, a link line for the
# wrong OS. So CMake doesn't decide; it asks:
#
#   jaal_target_is(<C++ expression> <out-var>)
#
# compiles `#if !(<expression>) #error` against the headers and sets <out-var>
# to the answer. It is a COMPILE check, never a run, so it is right when
# cross-compiling, and its result is cached (one tiny compile per question,
# at the first configure). Macros: anything jaal/meta/target.hpp or
# jaal/platform/backend.hpp defines (JAAL_OS_*, JAAL_CC_*, JAAL_PLATFORM_*).

include_guard(GLOBAL)
include(CheckCXXSourceCompiles)

set(_JAAL_TARGET_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../include" CACHE INTERNAL "")

function(jaal_target_is expr out)
    string(MAKE_C_IDENTIFIER "JAAL_TARGET_IS_${expr}" key)
    set(CMAKE_REQUIRED_INCLUDES "${_JAAL_TARGET_INCLUDE}")
    set(CMAKE_REQUIRED_QUIET ON)
    check_cxx_source_compiles("
#include <jaal/meta/target.hpp>
#include <jaal/platform/backend.hpp>
#if !(${expr})
#error no
#endif
int main() { return 0; }" ${key})
    if(${key})
        set(${out} ON PARENT_SCOPE)
    else()
        set(${out} OFF PARENT_SCOPE)
    endif()
endfunction()
