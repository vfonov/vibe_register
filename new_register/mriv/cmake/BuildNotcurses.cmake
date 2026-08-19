# Downloads, builds, and statically links notcurses from source via
# ExternalProject_Add(), for environments without a system notcurses install
# (or when a fully static mriv binary is wanted regardless). Opt in with
# -DMRIV_BUILD_NOTCURSES=ON; the default build keeps using a system package,
# located via find_path/find_library (see the caller in ../CMakeLists.txt).
#
# We build both notcurses-core-static AND notcurses-static (not just the
# former). Originally this file only built notcurses-core, on the theory
# that mriv's two entry-point families -- ncdirect_* (direct mode,
# src/render/Terminal.cpp) and ncvisual_from_rgba() -- live in
# src/lib/*.c (the notcurses-core target). That's true for
# ncvisual_from_rgba(), but notcurses_init()/ncdirect_init() themselves
# turned out to be defined in src/media/shim.c -- which upstream's
# CMakeLists.txt globs into src/media/*.c, i.e. the "notcurses"
# (multimedia) target, not notcurses-core (confirmed by reading both the
# shim.c source and the GLOB lines in notcurses' own CMakeLists.txt).
# So the init/teardown entry points require linking notcurses-static too.
#
# This does NOT reintroduce ffmpeg/OpenImageIO/qrcodegen as dependencies:
# with -DUSE_MULTIMEDIA=none (below), the "notcurses" target's actual
# file-decoding sources are swapped for a harmless src/media/none.c stub
# backend, so notcurses-static still needs nothing beyond what's already
# required for notcurses-core.
#
# This also sidesteps the exact problem noted in ../CMakeLists.txt for a
# *system-packaged* notcurses: a distro build's static archive doesn't
# carry its optional transitive deps (qrcodegen, gpm, libdeflate) in its
# .pc file, so linking it statically fails with undefined references.
# Here we choose every optional dependency ourselves (all off, except
# zlib in place of libdeflate), so the static archives' transitive deps
# are fully known and listed explicitly below.
#
# libunistring is itself built as a contained ExternalProject rather than
# required from the system: unlike zlib (a near-universal base-OS package),
# libunistring-dev is frequently absent even on otherwise well-provisioned
# hosts (confirmed on this repo's own dev sandbox), and it is a hard,
# non-optional build requirement of notcurses -- there is no
# USE_UNISTRING=OFF escape hatch upstream. Building it ourselves means
# MRIV_BUILD_NOTCURSES=ON has no dependency beyond a C/C++ toolchain, git,
# and the near-universal zlib dev package.
#
# tinfo is built the same way, for a different reason: it isn't a
# near-universal package at all in pkg-config-discoverable form. Linux
# distros split libtinfo out of ncurses and ship a tinfo.pc; macOS's system
# ncurses ships neither a tinfo.pc nor even a pkg-config executable, and
# Homebrew's ncurses formula doesn't split out tinfo either (see
# ../CLAUDE.md's build notes). Rather than depend on a system pkg-config
# install just to *locate* system tinfo -- which notcurses' own upstream
# CMakeLists.txt also unconditionally requires via
# find_package(PkgConfig REQUIRED) -- this file builds tinfo from source
# and patches notcurses' vendored CMakeLists.txt (see
# patch_notcurses_tinfo.cmake) to consume it directly, sidestepping
# pkg-config entirely for MRIV_BUILD_NOTCURSES=ON.

include(ExternalProject)

# The parent forces CMAKE_FIND_LIBRARY_SUFFIXES to prefer .a globally (see
# new_register/CMakeLists.txt, static-linking section) so libminc's own
# dependencies resolve statically -- and that setting is still in effect in
# this directory scope. We only want base-OS runtime libraries (libm, librt)
# to stay dynamic like any other Linux binary -- notcurses, libunistring,
# and tinfo are all built and linked statically by this file. Left unset,
# find_library(m)/find_library(rt) below would silently pick up glibc's
# static archives instead, which drags in surprising extra undefined symbols
# (e.g. _dl_x86_cpu_features) and produces a PIE binary with text
# relocations.
set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".a")

set(MRIV_NOTCURSES_VERSION "v3.0.17" CACHE STRING
    "notcurses git tag to build when MRIV_BUILD_NOTCURSES is ON")
set(MRIV_UNISTRING_VERSION "1.4.2" CACHE STRING
    "libunistring release version to build when MRIV_BUILD_NOTCURSES is ON")
set(MRIV_UNISTRING_SHA256 "e82664b170064e62331962126b259d452d53b227bb4a93ab20040d846fec01d8" CACHE STRING
    "sha256 of the libunistring-\${MRIV_UNISTRING_VERSION}.tar.gz release tarball")
set(MRIV_TINFO_VERSION "6.5" CACHE STRING
    "ncurses release version to build the self-contained libtinfo from, when MRIV_BUILD_NOTCURSES is ON")
set(MRIV_TINFO_SHA256 "136d91bc269a9a5785e5f9e980bc76ab57428f604ce3e5a5a90cebc767971cc6" CACHE STRING
    "sha256 of the ncurses-\${MRIV_TINFO_VERSION}.tar.gz release tarball")

set(MRIV_NOTCURSES_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/notcurses-external")
set(MRIV_NOTCURSES_INSTALL "${MRIV_NOTCURSES_PREFIX}/install")
set(MRIV_UNISTRING_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/unistring-external")
set(MRIV_UNISTRING_INSTALL "${MRIV_UNISTRING_PREFIX}/install")
set(MRIV_TINFO_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/tinfo-external")
set(MRIV_TINFO_INSTALL "${MRIV_TINFO_PREFIX}/install")

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_library(MRIV_LIBRT rt)
find_library(MRIV_LIBM m)

# --- libunistring (contained, static) --------------------------------------
#
# GNU autotools, not CMake -- release tarballs ship a pre-generated
# `configure` script, so no autoconf/automake/libtool is needed on the build
# host, just make + a C compiler (already required for everything else in
# this repo). --with-pic forces libtool to compile the static archive's
# objects as position-independent even though we're not building the shared
# library, matching how notcurses-core-static and mriv itself get linked
# into a PIE executable by default on modern Linux distros.
ExternalProject_Add(unistring_external
    URL                       "https://ftp.gnu.org/gnu/libunistring/libunistring-${MRIV_UNISTRING_VERSION}.tar.gz"
    URL_HASH                  SHA256=${MRIV_UNISTRING_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX                    "${MRIV_UNISTRING_PREFIX}"
    CONFIGURE_COMMAND         <SOURCE_DIR>/configure
                                  --prefix=${MRIV_UNISTRING_INSTALL}
                                  --disable-shared
                                  --enable-static
                                  --with-pic
                                  --disable-dependency-tracking
                                  "CC=${CMAKE_C_COMPILER}"
    BUILD_COMMAND             make
    INSTALL_COMMAND           make install
    BUILD_BYPRODUCTS          "${MRIV_UNISTRING_INSTALL}/lib/libunistring.a"
)

set(MRIV_UNISTRING_INCLUDE_DIR "${MRIV_UNISTRING_INSTALL}/include")
file(MAKE_DIRECTORY "${MRIV_UNISTRING_INCLUDE_DIR}")

add_library(unistring_static STATIC IMPORTED GLOBAL)
set_target_properties(unistring_static PROPERTIES
    IMPORTED_LOCATION             "${MRIV_UNISTRING_INSTALL}/lib/libunistring.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MRIV_UNISTRING_INCLUDE_DIR}"
)
add_dependencies(unistring_static unistring_external)

# --- libtinfo (contained, static) -------------------------------------------
#
# GNU autotools, not CMake -- release tarballs ship a pre-generated
# `configure` script. --with-termlib produces a separate libtinfo(.a)
# rather than folding terminfo access into the full curses library (mriv
# only ever needs the low-level terminfo functions notcurses itself calls
# -- setupterm/tigetstr/tparm/putp/del_curterm -- never full-screen curses).
# --disable-widec keeps the library named plainly "tinfo" (a wide-character
# build gets a "w" suffix -- "tinfow" -- which the patch below does not
# handle); mriv doesn't need wide-character terminfo functions regardless.
# --without-progs/--without-manpages/--without-tests/--disable-db-install
# trim the build to just the library and headers: we deliberately reuse the
# *system's* terminfo database (e.g. /usr/share/terminfo, already present
# on any real terminal-capable host) at runtime rather than bundling one --
# --disable-db-install in particular matters because without it, `make
# install` tries to compile and install a terminfo database into the
# configured terminfo directory (confirmed by a standalone trial build:
# with no override it picked up this build's own $TERMINFO env var and
# attempted to write into a live system path). --with-terminfo-dirs and
# --with-default-terminfo-dir pin the compiled-in runtime search path to
# standard Linux/Homebrew locations so the result doesn't depend on
# whatever $TERMINFO happens to be set in the build environment.
ExternalProject_Add(tinfo_external
    URL                       "https://ftp.gnu.org/gnu/ncurses/ncurses-${MRIV_TINFO_VERSION}.tar.gz"
    URL_HASH                  SHA256=${MRIV_TINFO_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    PREFIX                    "${MRIV_TINFO_PREFIX}"
    CONFIGURE_COMMAND         ${CMAKE_COMMAND} -E env --unset=TERMINFO --unset=TERMINFOS
                                  <SOURCE_DIR>/configure
                                  --prefix=${MRIV_TINFO_INSTALL}
                                  --with-termlib
                                  --disable-widec
                                  --without-shared
                                  --without-debug
                                  --without-profile
                                  --without-ada
                                  --without-cxx
                                  --without-cxx-binding
                                  --without-manpages
                                  --without-progs
                                  --without-tests
                                  --disable-db-install
                                  --disable-stripping
                                  --with-terminfo-dirs=/usr/share/terminfo:/usr/local/share/terminfo:/opt/homebrew/share/terminfo:/opt/homebrew/opt/ncurses/share/terminfo
                                  --with-default-terminfo-dir=/usr/share/terminfo
                                  "CC=${CMAKE_C_COMPILER}"
                                  "CFLAGS=-fPIC"
    BUILD_COMMAND             make
    INSTALL_COMMAND           make install
    BUILD_BYPRODUCTS          "${MRIV_TINFO_INSTALL}/lib/libtinfo.a"
)

# term.h's own `#include <ncurses/ncurses_dll.h>` only resolves with
# .../include on the path, while notcurses' sources `#include <term.h>`
# unqualified, which only resolves with .../include/ncurses on the path --
# both directories are required together (confirmed by a standalone trial
# compile/link against this same tinfo build).
set(MRIV_TINFO_INCLUDE_DIR "${MRIV_TINFO_INSTALL}/include")
set(MRIV_TINFO_INCLUDE_DIR2 "${MRIV_TINFO_INSTALL}/include/ncurses")
file(MAKE_DIRECTORY "${MRIV_TINFO_INCLUDE_DIR}")
file(MAKE_DIRECTORY "${MRIV_TINFO_INCLUDE_DIR2}")

add_library(tinfo_static STATIC IMPORTED GLOBAL)
set_target_properties(tinfo_static PROPERTIES
    IMPORTED_LOCATION             "${MRIV_TINFO_INSTALL}/lib/libtinfo.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MRIV_TINFO_INCLUDE_DIR};${MRIV_TINFO_INCLUDE_DIR2}"
)
add_dependencies(tinfo_static tinfo_external)

# --- notcurses (contained, static) ------------------------------------------
ExternalProject_Add(notcurses_external
    GIT_REPOSITORY      https://github.com/dankamongmen/notcurses.git
    GIT_TAG             "${MRIV_NOTCURSES_VERSION}"
    GIT_SHALLOW         TRUE
    # The tag is pinned; skip notcurses_external's routine "check for
    # upstream changes" network round-trip on every configure once the
    # first clone has landed.
    UPDATE_DISCONNECTED TRUE
    PREFIX              "${MRIV_NOTCURSES_PREFIX}"
    # unistring_external must finish installing before notcurses's own
    # configure step goes looking for unigbrk.h / libunistring.a; likewise
    # tinfo_external must finish before the PATCH_COMMAND below writes
    # (already-known, hardcoded) paths into notcurses' tinfo_external
    # install tree.
    DEPENDS             unistring_external tinfo_external
    # Replace upstream's find_package(PkgConfig REQUIRED) + tinfo lookup
    # with a direct reference to mriv's self-built libtinfo, so this build
    # has no dependency on a system pkg-config install. See
    # patch_notcurses_tinfo.cmake for why this is a patch rather than a
    # CMAKE_ARGS override: TERMINFO_* are plain local variables upstream
    # computes from pkg_search_module(), not cache entries a -D can pre-seed.
    PATCH_COMMAND       ${CMAKE_COMMAND}
                            -DMRIV_PATCH_FILE=<SOURCE_DIR>/CMakeLists.txt
                            -DMRIV_TINFO_INCLUDE_DIR=${MRIV_TINFO_INCLUDE_DIR}
                            -DMRIV_TINFO_INCLUDE_DIR2=${MRIV_TINFO_INCLUDE_DIR2}
                            -DMRIV_TINFO_LIBRARY=${MRIV_TINFO_INSTALL}/lib/libtinfo.a
                            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_notcurses_tinfo.cmake
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${MRIV_NOTCURSES_INSTALL}
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        # Our just-built libunistring, found via notcurses's own
        # find_path(unigbrk.h)/find_library(unistring) -- CMAKE_PREFIX_PATH
        # is searched before plain system directories, so this copy wins
        # even on a host that also has libunistring-dev installed.
        -DCMAKE_PREFIX_PATH=${MRIV_UNISTRING_INSTALL}
        # Disable everything mriv doesn't call so this build needs nothing
        # beyond what's already required above (tinfo, unistring, zlib):
        -DUSE_MULTIMEDIA=none      # no ffmpeg / OpenImageIO
        -DUSE_CXX=OFF              # no notcurses++ (ncpp:: is banned here anyway)
        -DUSE_DEFLATE=OFF          # zlib instead of libdeflate
        -DUSE_QRCODEGEN=OFF
        -DUSE_GPM=OFF
        -DUSE_PANDOC=OFF           # no man-page/HTML build, no pandoc dep
        -DUSE_DOXYGEN=OFF
        -DUSE_POC=OFF
        -DBUILD_EXECUTABLES=OFF    # no notcurses-demo / ncplayer / etc.
        -DBUILD_FFI_LIBRARY=OFF
        -DBUILD_TESTING=OFF        # no doctest dependency
        -DUSE_STATIC=ON
    BUILD_COMMAND       ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel
    BUILD_BYPRODUCTS    "${MRIV_NOTCURSES_INSTALL}/lib/libnotcurses-core.a"
                        "${MRIV_NOTCURSES_INSTALL}/lib/libnotcurses.a"
    INSTALL_DIR         "${MRIV_NOTCURSES_INSTALL}"
)

# ExternalProject_Add()'s build+install steps run at *build* time, not
# configure time -- the archive doesn't exist yet when the code below
# runs. Point an IMPORTED target at where the install step will put it
# (CMAKE_INSTALL_LIBDIR is pinned to "lib" above so this path is
# deterministic) and register the dependency so anything linking
# notcurses_core_static waits for the external build to finish first.
# IMPORTED targets require INTERFACE_INCLUDE_DIRECTORIES to exist at
# generate time even though the headers only arrive once the build runs,
# so create the directory now.
set(MRIV_NOTCURSES_INCLUDE_DIR "${MRIV_NOTCURSES_INSTALL}/include")
file(MAKE_DIRECTORY "${MRIV_NOTCURSES_INCLUDE_DIR}")

add_library(notcurses_core_static STATIC IMPORTED GLOBAL)
set_target_properties(notcurses_core_static PROPERTIES
    IMPORTED_LOCATION             "${MRIV_NOTCURSES_INSTALL}/lib/libnotcurses-core.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MRIV_NOTCURSES_INCLUDE_DIR}"
)
add_dependencies(notcurses_core_static notcurses_external)

# notcurses_init()/ncdirect_init() live here, not in notcurses-core (see the
# file-header comment above). INTERFACE_LINK_LIBRARIES records that this
# archive depends on notcurses_core_static, so CMake places both archives on
# the final link line in the right order (and repeats either one if their
# symbol references turn out to be mutually recursive) without mriv_notcurses
# having to list notcurses_core_static a second time itself.
add_library(notcurses_static STATIC IMPORTED GLOBAL)
set_target_properties(notcurses_static PROPERTIES
    IMPORTED_LOCATION             "${MRIV_NOTCURSES_INSTALL}/lib/libnotcurses.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MRIV_NOTCURSES_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES      notcurses_core_static
)
add_dependencies(notcurses_static notcurses_external)

# mriv_notcurses is the name both CMakeLists.txt branches (system package
# vs. this from-source build) expose to the rest of this subproject, so
# the choice of MRIV_BUILD_NOTCURSES is invisible past this point.
add_library(mriv_notcurses INTERFACE)
target_link_libraries(mriv_notcurses INTERFACE
    notcurses_static
    unistring_static
    tinfo_static
    ZLIB::ZLIB
    Threads::Threads
)
if(MRIV_LIBRT)
    target_link_libraries(mriv_notcurses INTERFACE "${MRIV_LIBRT}")
endif()
if(MRIV_LIBM)
    target_link_libraries(mriv_notcurses INTERFACE "${MRIV_LIBM}")
endif()
