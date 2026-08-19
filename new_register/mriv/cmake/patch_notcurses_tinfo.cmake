# Patches notcurses' vendored CMakeLists.txt so its build does not require a
# system pkg-config install. Invoked as notcurses_external's PATCH_COMMAND
# (see BuildNotcurses.cmake) via:
#   cmake -DMRIV_PATCH_FILE=... -DMRIV_TINFO_INCLUDE_DIR=... \
#         -DMRIV_TINFO_INCLUDE_DIR2=... -DMRIV_TINFO_LIBRARY=... \
#         -P patch_notcurses_tinfo.cmake
#
# Two include dirs are needed because ncurses' own installed term.h does
# `#include <ncurses/ncurses_dll.h>` -- resolvable only with prefix/include
# on the path -- while notcurses' sources `#include <term.h>` unqualified,
# resolvable only with prefix/include/ncurses on the path. Confirmed by a
# standalone trial build/link against the self-built tinfo before writing
# this patch; both dirs are required together.
#
# Upstream notcurses (CMakeLists.txt) unconditionally does:
#   find_package(PkgConfig REQUIRED)
#   pkg_search_module(TERMINFO REQUIRED tinfo>=6.1 ncursesw>=6.1)
# to locate terminfo. That is a hard requirement on the *system* having a
# pkg-config executable -- which this repo's self-contained build
# (MRIV_BUILD_NOTCURSES=ON) is specifically trying to avoid, since it also
# builds its own libtinfo from source (see the tinfo_external target in
# BuildNotcurses.cmake). Rather than requiring a bootstrapped private
# pkg-config just to satisfy this one upstream lookup, this patch replaces
# the pkg-config call with a direct set() of the same variables upstream
# consumes afterwards (TERMINFO_INCLUDE_DIRS, TERMINFO_LIBRARIES, etc.),
# pointed at mriv's own tinfo_external build.

foreach(_required MRIV_PATCH_FILE MRIV_TINFO_INCLUDE_DIR MRIV_TINFO_INCLUDE_DIR2 MRIV_TINFO_LIBRARY)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "patch_notcurses_tinfo.cmake: ${_required} not set")
    endif()
endforeach()

file(READ "${MRIV_PATCH_FILE}" _content)

set(_needle [[find_package(PkgConfig REQUIRED)
# some distros (<cough>motherfucking alpine</cough> subsume terminfo directly
# into ncurses. accept either, and may god have mercy on our souls.
pkg_search_module(TERMINFO REQUIRED tinfo>=6.1 ncursesw>=6.1)]])

string(FIND "${_content}" "${_needle}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR
        "patch_notcurses_tinfo.cmake: expected pkg-config/tinfo block not "
        "found in ${MRIV_PATCH_FILE}. notcurses' upstream CMakeLists.txt "
        "may have changed -- update MRIV_NOTCURSES_VERSION's patch to match, "
        "or re-pin to the version this patch was written against.")
endif()

set(_replacement "# Patched by mriv (see mriv/cmake/patch_notcurses_tinfo.cmake): upstream's
# pkg-config-based terminfo lookup replaced with mriv's self-built,
# statically-linked libtinfo. This build has no dependency on a system
# pkg-config install.
set(TERMINFO_INCLUDE_DIRS \"${MRIV_TINFO_INCLUDE_DIR}\" \"${MRIV_TINFO_INCLUDE_DIR2}\")
set(TERMINFO_STATIC_INCLUDE_DIRS \"${MRIV_TINFO_INCLUDE_DIR}\" \"${MRIV_TINFO_INCLUDE_DIR2}\")
set(TERMINFO_LIBRARIES \"${MRIV_TINFO_LIBRARY}\")
set(TERMINFO_STATIC_LIBRARIES \"${MRIV_TINFO_LIBRARY}\")
set(TERMINFO_LIBRARY_DIRS \"\")
set(TERMINFO_STATIC_LIBRARY_DIRS \"\")")

string(REPLACE "${_needle}" "${_replacement}" _content "${_content}")
file(WRITE "${MRIV_PATCH_FILE}" "${_content}")
